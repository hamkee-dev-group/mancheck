#define _XOPEN_SOURCE 700

#include "mc_compdb.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static char *
mc_strdup_range(const char *start, const char *end)
{
    size_t len = (size_t)(end - start);
    char *out = malloc(len + 1);
    if (!out)
        return NULL;

    memcpy(out, start, len);
    out[len] = '\0';
    return out;
}

static char *
mc_read_text_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;

    if (fseek(f, 0, SEEK_END) != 0) {
        (void)fclose(f);
        return NULL;
    }

    long len = ftell(f);
    if (len < 0) {
        (void)fclose(f);
        return NULL;
    }

    if (fseek(f, 0, SEEK_SET) != 0) {
        (void)fclose(f);
        return NULL;
    }

    char *buf = malloc((size_t)len + 1);
    if (!buf) {
        (void)fclose(f);
        return NULL;
    }

    size_t n = fread(buf, 1, (size_t)len, f);
    if (n != (size_t)len && ferror(f)) {
        free(buf);
        (void)fclose(f);
        return NULL;
    }

    buf[n] = '\0';
    (void)fclose(f);
    return buf;
}

static char *
mc_path_dirname(const char *path)
{
    const char *slash = strrchr(path, '/');

    if (!slash)
        return strdup(".");
    if (slash == path)
        return strdup("/");

    return mc_strdup_range(path, slash);
}

static char *
mc_path_join(const char *base, const char *path)
{
    size_t base_len = strlen(base);
    size_t path_len = strlen(path);
    int need_slash = (base_len > 0 && base[base_len - 1] != '/');
    char *out = malloc(base_len + (size_t)need_slash + path_len + 1);
    if (!out)
        return NULL;

    memcpy(out, base, base_len);
    if (need_slash)
        out[base_len++] = '/';
    memcpy(out + base_len, path, path_len);
    out[base_len + path_len] = '\0';
    return out;
}

static char *
mc_resolve_path(const char *base, const char *path)
{
    char *joined = NULL;
    char *resolved;

    if (!path)
        return NULL;

    if (path[0] == '/') {
        joined = strdup(path);
    } else {
        joined = mc_path_join(base, path);
    }

    if (!joined)
        return NULL;

    resolved = realpath(joined, NULL);
    if (resolved) {
        free(joined);
        return resolved;
    }

    return joined;
}

static char *
mc_normalize_lookup_path(const char *path)
{
    char *resolved;

    if (!path)
        return NULL;

    resolved = realpath(path, NULL);
    if (resolved)
        return resolved;

    return strdup(path);
}

static char *
mc_join_shell_quoted_argv(char **argv, size_t argc)
{
    size_t len = 1;

    for (size_t i = 0; i < argc; i++) {
        const char *arg = argv[i] ? argv[i] : "";

        len += 2;
        for (const char *p = arg; *p; p++)
            len += (*p == '\'') ? 4 : 1;
        if (i + 1 < argc)
            len++;
    }

    char *out = malloc(len);
    if (!out)
        return NULL;

    char *dst = out;
    for (size_t i = 0; i < argc; i++) {
        const char *arg = argv[i] ? argv[i] : "";

        *dst++ = '\'';
        for (const char *p = arg; *p; p++) {
            if (*p == '\'') {
                memcpy(dst, "'\\''", 4);
                dst += 4;
            } else {
                *dst++ = *p;
            }
        }
        *dst++ = '\'';

        if (i + 1 < argc)
            *dst++ = ' ';
    }

    *dst = '\0';
    return out;
}

static void
mc_compile_db_entry_free(struct mc_compile_db_entry *entry)
{
    if (!entry)
        return;

    free(entry->directory);
    free(entry->file);
    free(entry->resolved_file);
    free(entry->command);
    if (entry->arguments) {
        for (size_t i = 0; i < entry->arg_count; i++)
            free(entry->arguments[i]);
    }
    free(entry->arguments);
    memset(entry, 0, sizeof(*entry));
}

void
mc_compile_db_free(struct mc_compile_db *db)
{
    if (!db)
        return;

    for (size_t i = 0; i < db->count; i++)
        mc_compile_db_entry_free(&db->entries[i]);
    free(db->entries);
    db->entries = NULL;
    db->count = 0;
}

static const char *
mc_json_skip_ws(const char *p)
{
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
        p++;
    return p;
}

static const char *
mc_json_parse_string(const char *p, char **out)
{
    char *buf;
    size_t len = 0;

    if (*p != '"')
        return NULL;

    p++;
    buf = malloc(strlen(p) + 1);
    if (!buf)
        return NULL;

    while (*p && *p != '"') {
        if (*p == '\\') {
            p++;
            if (*p == '\0') {
                free(buf);
                return NULL;
            }

            switch (*p) {
            case '"': buf[len++] = '"'; break;
            case '\\': buf[len++] = '\\'; break;
            case '/': buf[len++] = '/'; break;
            case 'b': buf[len++] = '\b'; break;
            case 'f': buf[len++] = '\f'; break;
            case 'n': buf[len++] = '\n'; break;
            case 'r': buf[len++] = '\r'; break;
            case 't': buf[len++] = '\t'; break;
            default:
                free(buf);
                return NULL;
            }
            p++;
            continue;
        }

        buf[len++] = *p++;
    }

    if (*p != '"') {
        free(buf);
        return NULL;
    }

    buf[len] = '\0';
    *out = buf;
    return p + 1;
}

static const char *
mc_json_skip_value(const char *p)
{
    int depth;

    p = mc_json_skip_ws(p);

    if (*p == '"') {
        char *tmp = NULL;
        const char *next = mc_json_parse_string(p, &tmp);
        free(tmp);
        return next;
    }

    if (*p == '{') {
        depth = 1;
        p++;
        while (*p && depth > 0) {
            if (*p == '"') {
                char *tmp = NULL;
                p = mc_json_parse_string(p, &tmp);
                free(tmp);
                if (!p)
                    return NULL;
                continue;
            }
            if (*p == '{' || *p == '[')
                depth++;
            else if (*p == '}' || *p == ']')
                depth--;
            p++;
        }
        return depth == 0 ? p : NULL;
    }

    if (*p == '[') {
        depth = 1;
        p++;
        while (*p && depth > 0) {
            if (*p == '"') {
                char *tmp = NULL;
                p = mc_json_parse_string(p, &tmp);
                free(tmp);
                if (!p)
                    return NULL;
                continue;
            }
            if (*p == '[' || *p == '{')
                depth++;
            else if (*p == ']' || *p == '}')
                depth--;
            p++;
        }
        return depth == 0 ? p : NULL;
    }

    while (*p &&
           *p != ',' &&
           *p != ']' &&
           *p != '}' &&
           *p != ' ' &&
           *p != '\t' &&
           *p != '\n' &&
           *p != '\r')
        p++;

    return p;
}

static const char *
mc_json_parse_string_array(const char *p, char ***out, size_t *out_count)
{
    char **items = NULL;
    size_t count = 0;

    if (*p != '[')
        return NULL;

    p = mc_json_skip_ws(p + 1);
    if (*p == ']') {
        *out = NULL;
        *out_count = 0;
        return p + 1;
    }

    for (;;) {
        char *item = NULL;
        char **tmp;

        p = mc_json_parse_string(p, &item);
        if (!p)
            goto fail;

        tmp = realloc(items, (count + 1) * sizeof(*tmp));
        if (!tmp) {
            free(item);
            goto fail;
        }
        items = tmp;
        items[count++] = item;

        p = mc_json_skip_ws(p);
        if (*p == ',') {
            p = mc_json_skip_ws(p + 1);
            continue;
        }
        if (*p == ']') {
            *out = items;
            *out_count = count;
            return p + 1;
        }
        goto fail;
    }

fail:
    if (items) {
        for (size_t i = 0; i < count; i++)
            free(items[i]);
    }
    free(items);
    return NULL;
}

static int
mc_compile_db_push_entry(struct mc_compile_db *db,
                         struct mc_compile_db_entry *entry)
{
    struct mc_compile_db_entry *tmp;

    tmp = realloc(db->entries, (db->count + 1) * sizeof(*tmp));
    if (!tmp)
        return -1;

    db->entries = tmp;
    db->entries[db->count++] = *entry;
    memset(entry, 0, sizeof(*entry));
    return 0;
}

int
mc_load_compile_db(const char *path, struct mc_compile_db *db)
{
    char *text = NULL;
    char *db_dir = NULL;
    const char *p;

    memset(db, 0, sizeof(*db));

    text = mc_read_text_file(path);
    if (!text)
        return -1;

    db_dir = mc_path_dirname(path);
    if (!db_dir) {
        free(text);
        return -1;
    }

    p = mc_json_skip_ws(text);
    if (*p != '[')
        goto fail;

    p = mc_json_skip_ws(p + 1);
    if (*p == ']') {
        free(db_dir);
        free(text);
        return 0;
    }

    for (;;) {
        struct mc_compile_db_entry entry;

        memset(&entry, 0, sizeof(entry));

        if (*p != '{')
            goto fail;
        p = mc_json_skip_ws(p + 1);

        if (*p != '}') {
            for (;;) {
                char *key = NULL;

                p = mc_json_parse_string(p, &key);
                if (!p)
                    goto entry_fail;

                p = mc_json_skip_ws(p);
                if (*p != ':') {
                    free(key);
                    goto entry_fail;
                }
                p = mc_json_skip_ws(p + 1);

                if (strcmp(key, "directory") == 0) {
                    free(entry.directory);
                    p = mc_json_parse_string(p, &entry.directory);
                } else if (strcmp(key, "file") == 0) {
                    free(entry.file);
                    p = mc_json_parse_string(p, &entry.file);
                } else if (strcmp(key, "command") == 0) {
                    free(entry.command);
                    p = mc_json_parse_string(p, &entry.command);
                } else if (strcmp(key, "arguments") == 0) {
                    if (entry.arguments) {
                        for (size_t i = 0; i < entry.arg_count; i++)
                            free(entry.arguments[i]);
                        free(entry.arguments);
                        entry.arguments = NULL;
                        entry.arg_count = 0;
                    }
                    p = mc_json_parse_string_array(p,
                                                   &entry.arguments,
                                                   &entry.arg_count);
                } else {
                    p = mc_json_skip_value(p);
                }

                free(key);

                if (!p)
                    goto entry_fail;

                p = mc_json_skip_ws(p);
                if (*p == ',') {
                    p = mc_json_skip_ws(p + 1);
                    continue;
                }
                if (*p == '}')
                    break;
                goto entry_fail;
            }
        }

        if (entry.directory) {
            char *resolved_dir = mc_resolve_path(db_dir, entry.directory);
            if (!resolved_dir)
                goto entry_fail;
            free(entry.directory);
            entry.directory = resolved_dir;
        } else {
            entry.directory = strdup(db_dir);
            if (!entry.directory)
                goto entry_fail;
        }

        if (entry.file) {
            entry.resolved_file = mc_resolve_path(entry.directory, entry.file);
            if (!entry.resolved_file)
                goto entry_fail;
        }

        if (!entry.command && entry.arguments && entry.arg_count > 0) {
            entry.command = mc_join_shell_quoted_argv(entry.arguments,
                                                      entry.arg_count);
            if (!entry.command)
                goto entry_fail;
        }

        if (mc_compile_db_push_entry(db, &entry) != 0)
            goto entry_fail;

        p = mc_json_skip_ws(p + 1);
        if (*p == ',') {
            p = mc_json_skip_ws(p + 1);
            continue;
        }
        if (*p == ']') {
            free(db_dir);
            free(text);
            return 0;
        }
        goto fail;

entry_fail:
        mc_compile_db_entry_free(&entry);
        goto fail;
    }

fail:
    free(db_dir);
    free(text);
    mc_compile_db_free(db);
    return -1;
}

const struct mc_compile_db_entry *
mc_find_compile_db_entry(const struct mc_compile_db *db, const char *abs_path)
{
    char *lookup_path;
    const struct mc_compile_db_entry *match = NULL;

    if (!db || !abs_path)
        return NULL;

    lookup_path = mc_normalize_lookup_path(abs_path);
    if (!lookup_path)
        return NULL;

    for (size_t i = 0; i < db->count; i++) {
        if (db->entries[i].resolved_file &&
            strcmp(db->entries[i].resolved_file, lookup_path) == 0) {
            match = &db->entries[i];
            break;
        }
    }

    free(lookup_path);
    return match;
}

const char *
mc_compdb_lookup(const struct mc_compile_db *db, const char *abs_path)
{
    const struct mc_compile_db_entry *entry =
        mc_find_compile_db_entry(db, abs_path);

    return entry ? entry->command : NULL;
}

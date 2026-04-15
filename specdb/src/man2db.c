// specdb/src/man2db.c
#define _XOPEN_SOURCE 700
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <ctype.h>
#include <sqlite3.h>
#include "specdb.h"

struct parsed_section {
    char *name;
    char *content;
    int   order_index;
};

struct parsed_manpage {
    char *raw;          /* full man text */
    char *short_name;   /* from NAME */
    char *short_desc;   /* from NAME */
    char *header;       /* from SYNOPSIS (best-effort) */
    char *proto;        /* from SYNOPSIS (best-effort) */
    struct parsed_section *sections;
    int n_sections;
};

struct name_section {
    char *name;
    char *section;
};

/* ---------------- utilities ---------------- */

static void die(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    exit(EXIT_FAILURE);
}

static char *xstrdup(const char *s)
{
    if (!s) return NULL;
    size_t len = strlen(s);
    char *p = malloc(len + 1);
    if (!p) die("out of memory");
    memcpy(p, s, len + 1);
    return p;
}

static char *read_command_output(const char *fmt, ...)
{
    char cmd[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(cmd, sizeof(cmd), fmt, ap);
    va_end(ap);

    /* Capture both stdout and stderr, we'll filter "warning:" lines out */
    char full_cmd[1200];
    snprintf(full_cmd, sizeof(full_cmd), "%s 2>&1", cmd);

    FILE *fp = popen(full_cmd, "r");
    if (!fp) die("popen failed: %s", strerror(errno));

    /* First read raw output */
    size_t cap = 16384;
    size_t len = 0;
    char *buf = malloc(cap);
    if (!buf) die("malloc failed");

    size_t n;
    while ((n = fread(buf + len, 1, cap - len, fp)) > 0) {
        len += n;
        if (len == cap) {
            cap *= 2;
            char *tmp = realloc(buf, cap);
            if (!tmp) die("realloc failed");
            buf = tmp;
        }
    }
    buf[len] = '\0';

    int status = pclose(fp);
    if (status == -1) {
        fprintf(stderr, "warning: pclose failed for cmd: %s\n", full_cmd);
    }

    /* Now filter out any lines starting with "warning:" */
    char *filtered = malloc(len + 1);
    if (!filtered) die("malloc failed");
    size_t out_len = 0;

    const char *p = buf;
    const char *end = buf + len;
    while (p < end) {
        const char *line_start = p;
        while (p < end && *p != '\n') p++;
        const char *line_end = p;
        if (p < end && *p == '\n') p++;

        size_t line_len = (size_t)(line_end - line_start);

        /* check if line begins with "warning:" (ignoring leading spaces) */
        const char *t = line_start;
        while (t < line_end && (*t == ' ' || *t == '\t')) t++;
        int is_warning_line = 0;
        if ((size_t)(line_end - t) >= 8 &&
            strncmp(t, "warning:", 8) == 0) {
            is_warning_line = 1;
        }

        if (!is_warning_line) {
            memcpy(filtered + out_len, line_start, line_len);
            out_len += line_len;
            if (p <= end) {
                filtered[out_len++] = '\n';
            }
        }
    }

    filtered[out_len] = '\0';
    free(buf);
    return filtered;
}

/* trim leading/trailing spaces/newlines */
static char *trim_new(const char *start, const char *end)
{
    while (start < end && (*start == ' ' || *start == '\t' || *start == '\n'))
        start++;
    while (end > start && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\n'))
        end--;
    size_t len = (size_t)(end - start);
    char *s = malloc(len + 1);
    if (!s) die("malloc failed");
    memcpy(s, start, len);
    s[len] = '\0';
    return s;
}

/* ---------------- section parsing ---------------- */

static int is_section_header_line(const char *line, size_t len)
{
    /* Heuristic: short line, mostly uppercase and spaces/punct */
    if (len == 0 || len > 40) return 0;

    int has_alpha = 0;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)line[i];
        if (c == ' ' || c == '\t') continue;
        if (isalpha(c)) {
            has_alpha = 1;
            if (!isupper(c)) return 0;
        } else if (!ispunct(c) && !isdigit(c)) {
            return 0;
        }
    }
    return has_alpha;
}

/* Splits raw man text into sections by heuristic headers. */
static void parse_sections(const char *txt, struct parsed_manpage *pm)
{
    pm->sections = NULL;
    pm->n_sections = 0;

    size_t cap = 0;
    struct parsed_section *secs = NULL;

    const char *p = txt;
    const char *end = txt + strlen(txt);

    const char *cur_name = NULL;
    const char *cur_content_start = NULL;
    int ord = 0;

    while (p < end) {
        const char *line_start = p;
        while (p < end && *p != '\n') p++;
        const char *line_end = p;
        if (p < end && *p == '\n') p++;

        /* Compute trimmed line */
        const char *t_start = line_start;
        const char *t_end   = line_end;
        while (t_start < t_end && (*t_start == ' ' || *t_start == '\t')) t_start++;
        while (t_end > t_start && (t_end[-1] == ' ' || t_end[-1] == '\t')) t_end--;

        size_t t_len = (size_t)(t_end - t_start);

        if (t_len > 0 && is_section_header_line(t_start, t_len)) {
            /* New header */
            if (cur_name) {
                /* Close previous section */
                if (pm->n_sections == cap) {
                    cap = cap ? cap * 2 : 8;
                    struct parsed_section *tmp =
                        realloc(secs, (size_t)cap * sizeof(*tmp));
                    if (!tmp) die("realloc failed");
                    secs = tmp;
                }
                struct parsed_section *s = &secs[pm->n_sections];
                s->name = xstrdup(cur_name);
                s->content = trim_new(cur_content_start ? cur_content_start : line_start,
                                      line_start);
                s->order_index = ord++;
                pm->n_sections++;
                free((char *)cur_name);
            }
            /* start new section */
            cur_name = trim_new(t_start, t_end);
            cur_content_start = p;
        } else {
            /* normal body line; do nothing here */
        }
    }

    /* close last section if any */
    if (cur_name) {
        if (pm->n_sections == cap) {
            cap = cap ? cap * 2 : 8;
            struct parsed_section *tmp =
                realloc(secs, (size_t)cap * sizeof(*tmp));
            if (!tmp) die("realloc failed");
            secs = tmp;
        }
        struct parsed_section *s = &secs[pm->n_sections];
        s->name = xstrdup(cur_name);
        s->content = trim_new(cur_content_start ? cur_content_start : end, end);
        s->order_index = ord++;
        pm->n_sections++;
        free((char *)cur_name);
    }

    pm->sections = secs;
}

/* Extract NAME-based metadata from sections. */
static void extract_name_meta(struct parsed_manpage *pm)
{
    pm->short_name = NULL;
    pm->short_desc = NULL;

    for (int i = 0; i < pm->n_sections; i++) {
        struct parsed_section *s = &pm->sections[i];
        if (strcmp(s->name, "NAME") != 0) continue;

        const char *p = s->content;
        const char *end = s->content + strlen(s->content);

        /* first non-empty line */
        while (p < end) {
            const char *line_start = p;
            while (p < end && *p != '\n') p++;
            const char *line_end = p;
            if (p < end && *p == '\n') p++;

            /* trim line */
            const char *t_start = line_start;
            const char *t_end   = line_end;
            while (t_start < t_end && (*t_start == ' ' || *t_start == '\t'))
                t_start++;
            while (t_end > t_start && (t_end[-1] == ' ' || t_end[-1] == '\t'))
                t_end--;

            if (t_start >= t_end) continue; /* empty */

            /* line like: "read, write - read from file descriptor" */
            const char *dash = strstr(t_start, " - ");
            if (!dash || dash >= t_end) {
                pm->short_name = trim_new(t_start, t_end);
                pm->short_desc = NULL;
            } else {
                pm->short_name = trim_new(t_start, dash);
                pm->short_desc = trim_new(dash + 3, t_end);
            }
            return;
        }
    }
}

/* Extract SYNOPSIS-based metadata (header + first prototype). */
static void extract_synopsis_meta(struct parsed_manpage *pm)
{
    pm->header = NULL;
    pm->proto  = NULL;

    for (int i = 0; i < pm->n_sections; i++) {
        struct parsed_section *s = &pm->sections[i];
        if (strcmp(s->name, "SYNOPSIS") != 0) continue;

        const char *p = s->content;
        const char *end = s->content + strlen(s->content);

        while (p < end) {
            const char *line_start = p;
            while (p < end && *p != '\n') p++;
            const char *line_end = p;
            if (p < end && *p == '\n') p++;

            /* trimmed line */
            const char *t_start = line_start;
            const char *t_end   = line_end;
            while (t_start < t_end && (*t_start == ' ' || *t_start == '\t'))
                t_start++;
            while (t_end > t_start && (t_end[-1] == ' ' || t_end[-1] == '\t'))
                t_end--;

            size_t len = (size_t)(t_end - t_start);
            if (len == 0) continue;

            /* First, look for include */
            if (!pm->header && strstr(t_start, "#include") != NULL) {
                const char *lt = strchr(t_start, '<');
                const char *gt = strchr(t_start, '>');
                if (lt && gt && gt > lt) {
                    pm->header = trim_new(lt, gt + 1);
                } else {
                    /* fallback: whole line */
                    pm->header = trim_new(t_start, t_end);
                }
            }

            /* Then, look for a prototype line (very heuristic) */
            if (!pm->proto && strchr(t_start, '(') && strchr(t_start, ')')) {
                pm->proto = trim_new(t_start, t_end);
            }
        }
        break; /* only first SYNOPSIS */
    }
}

static void parsed_manpage_free(struct parsed_manpage *pm)
{
    if (!pm) return;
    free(pm->raw);
    free(pm->short_name);
    free(pm->short_desc);
    free(pm->header);
    free(pm->proto);
    if (pm->sections) {
        for (int i = 0; i < pm->n_sections; i++) {
            free(pm->sections[i].name);
            free(pm->sections[i].content);
        }
        free(pm->sections);
    }
}

/* ---------------- DB insertion + aliases ---------------- */

static int upsert_function(sqlite3 *db,
                           const char *name,
                           const char *section,
                           const struct parsed_manpage *pm,
                           const char *man_source,
                           int *out_function_id)
{
    const char *sql =
        "INSERT INTO functions(name, section, short_name, short_desc, header, proto, man_source, raw) "
        "VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8) "
        "ON CONFLICT(name, section) DO UPDATE SET "
        "  short_name=excluded.short_name,"
        "  short_desc=excluded.short_desc,"
        "  header=excluded.header,"
        "  proto=excluded.proto,"
        "  man_source=excluded.man_source,"
        "  raw=excluded.raw;";

    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "specdb-build: prepare failed: %s\n", sqlite3_errmsg(db));
        return -1;
    }

    sqlite3_bind_text(stmt, 1, name,    -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, section, -1, SQLITE_STATIC);
    if (pm->short_name) sqlite3_bind_text(stmt, 3, pm->short_name, -1, SQLITE_STATIC);
    else                sqlite3_bind_null(stmt, 3);
    if (pm->short_desc) sqlite3_bind_text(stmt, 4, pm->short_desc, -1, SQLITE_STATIC);
    else                sqlite3_bind_null(stmt, 4);
    if (pm->header)     sqlite3_bind_text(stmt, 5, pm->header,     -1, SQLITE_STATIC);
    else                sqlite3_bind_null(stmt, 5);
    if (pm->proto)      sqlite3_bind_text(stmt, 6, pm->proto,      -1, SQLITE_STATIC);
    else                sqlite3_bind_null(stmt, 6);
    if (man_source)     sqlite3_bind_text(stmt, 7, man_source,     -1, SQLITE_STATIC);
    else                sqlite3_bind_null(stmt, 7);
    if (pm->raw)        sqlite3_bind_text(stmt, 8, pm->raw,        -1, SQLITE_STATIC);
    else                sqlite3_bind_null(stmt, 8);

    int rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        fprintf(stderr, "specdb-build: step failed: %s\n", sqlite3_errmsg(db));
        sqlite3_finalize(stmt);
        return -1;
    }
    sqlite3_finalize(stmt);

    /* Get id back */
    const char *sql_id =
        "SELECT id FROM functions WHERE name = ?1 AND section = ?2;";
    if (sqlite3_prepare_v2(db, sql_id, -1, &stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "specdb-build: prepare id failed: %s\n", sqlite3_errmsg(db));
        return -1;
    }
    sqlite3_bind_text(stmt, 1, name,    -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, section, -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_ROW) {
        fprintf(stderr, "specdb-build: cannot retrieve id for %s(%s)\n", name, section);
        sqlite3_finalize(stmt);
        return -1;
    }
    int id = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);

    if (out_function_id) *out_function_id = id;
    return 0;
}

static int replace_sections(sqlite3 *db, int function_id, const struct parsed_manpage *pm)
{
    const char *sql_del =
        "DELETE FROM function_sections WHERE function_id = ?1;";
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, sql_del, -1, &stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "specdb-build: prepare delete failed: %s\n", sqlite3_errmsg(db));
        return -1;
    }
    sqlite3_bind_int(stmt, 1, function_id);
    int rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        fprintf(stderr, "specdb-build: delete failed: %s\n", sqlite3_errmsg(db));
        sqlite3_finalize(stmt);
        return -1;
    }
    sqlite3_finalize(stmt);

    const char *sql_ins =
        "INSERT INTO function_sections(function_id, section_name, content, ord) "
        "VALUES(?1, ?2, ?3, ?4);";

    if (sqlite3_prepare_v2(db, sql_ins, -1, &stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "specdb-build: prepare insert section failed: %s\n", sqlite3_errmsg(db));
        return -1;
    }

    for (int i = 0; i < pm->n_sections; i++) {
        struct parsed_section *s = &pm->sections[i];

        sqlite3_bind_int(stmt, 1, function_id);
        sqlite3_bind_text(stmt, 2, s->name,    -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 3, s->content, -1, SQLITE_STATIC);
        sqlite3_bind_int(stmt, 4, s->order_index);

        rc = sqlite3_step(stmt);
        if (rc != SQLITE_DONE) {
            fprintf(stderr, "specdb-build: insert section failed: %s\n", sqlite3_errmsg(db));
            sqlite3_finalize(stmt);
            return -1;
        }
        sqlite3_reset(stmt);
        sqlite3_clear_bindings(stmt);
    }
    sqlite3_finalize(stmt);
    return 0;
}

/* delete + reinsert aliases for this function */
static int replace_aliases(sqlite3 *db,
                           int function_id,
                           const char *canonical_name,
                           const struct parsed_manpage *pm)
{
    const char *sql_del =
        "DELETE FROM function_aliases WHERE function_id = ?1;";
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, sql_del, -1, &stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "specdb-build: prepare delete aliases failed: %s\n", sqlite3_errmsg(db));
        return -1;
    }
    sqlite3_bind_int(stmt, 1, function_id);
    int rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        fprintf(stderr, "specdb-build: delete aliases failed: %s\n", sqlite3_errmsg(db));
        sqlite3_finalize(stmt);
        return -1;
    }
    sqlite3_finalize(stmt);

    const char *sql_ins =
        "INSERT OR IGNORE INTO function_aliases(function_id, alias_name) "
        "VALUES(?1, ?2);";

    if (sqlite3_prepare_v2(db, sql_ins, -1, &stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "specdb-build: prepare insert alias failed: %s\n", sqlite3_errmsg(db));
        return -1;
    }

    /* Always insert canonical name as alias */
    sqlite3_bind_int(stmt, 1, function_id);
    sqlite3_bind_text(stmt, 2, canonical_name, -1, SQLITE_STATIC);
    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        fprintf(stderr, "specdb-build: insert alias failed: %s\n", sqlite3_errmsg(db));
        sqlite3_finalize(stmt);
        return -1;
    }
    sqlite3_reset(stmt);
    sqlite3_clear_bindings(stmt);

    /* If we have a NAME line with multiple names, split them. */
    if (pm->short_name) {
        char *tmp = xstrdup(pm->short_name);
        char *p = tmp;
        while (*p) {
            /* token = up to comma or end */
            char *start = p;
            while (*p && *p != ',') p++;
            char *end = p;
            if (*p == ',') p++;

            /* trim */
            while (start < end && (*start == ' ' || *start == '\t')) start++;
            while (end > start && (end[-1] == ' ' || end[-1] == '\t')) end--;

            if (start < end) {
                char *alias = trim_new(start, end);
                if (alias[0] != '\0') {
                    sqlite3_bind_int(stmt, 1, function_id);
                    sqlite3_bind_text(stmt, 2, alias, -1, SQLITE_STATIC);
                    rc = sqlite3_step(stmt);
                    if (rc != SQLITE_DONE) {
                        fprintf(stderr, "specdb-build: insert alias failed: %s\n", sqlite3_errmsg(db));
                        free(alias);
                        free(tmp);
                        sqlite3_finalize(stmt);
                        return -1;
                    }
                    sqlite3_reset(stmt);
                    sqlite3_clear_bindings(stmt);
                }
                free(alias);
            }
        }
        free(tmp);
    }

    sqlite3_finalize(stmt);
    return 0;
}

/* ---------------- enumeration with man -k ---------------- */

static void free_name_section_array(struct name_section *arr, int n)
{
    if (!arr) return;
    for (int i = 0; i < n; i++) {
        free(arr[i].name);
        free(arr[i].section);
    }
    free(arr);
}

/* Parse man -k output:
 *   name (section) - description
 * If section_filter is non-NULL, we call: man -k . <section_filter>
 * Else: man -k .
 */
static int enumerate_manpages(const char *section_filter,
                              struct name_section **out_arr,
                              int *out_n)
{
    *out_arr = NULL;
    *out_n   = 0;

    char *out = NULL;
    if (section_filter) {
        out = read_command_output("man -k . %s", section_filter);
    } else {
        out = read_command_output("man -k .");
    }
    if (!out || out[0] == '\0') {
        free(out);
        return 0;
    }

    struct name_section *arr = NULL;
    int n = 0, cap = 0;

    const char *p = out;
    const char *end = out + strlen(out);

    while (p < end) {
        const char *line_start = p;
        while (p < end && *p != '\n') p++;
        const char *line_end = p;
        if (p < end && *p == '\n') p++;

        /* trim spaces */
        const char *t_start = line_start;
        const char *t_end   = line_end;
        while (t_start < t_end && (*t_start == ' ' || *t_start == '\t')) t_start++;
        while (t_end > t_start && (t_end[-1] == ' ' || t_end[-1] == '\t')) t_end--;

        if (t_start >= t_end) continue;

        /* Find " (": name_before + "(" section ")" ... */
        const char *paren_open = strchr(t_start, '(');
        const char *paren_close = NULL;
        if (paren_open) {
            paren_close = strchr(paren_open, ')');
        }
        if (!paren_open || !paren_close || paren_close > t_end) {
            continue; /* not a standard line */
        }

        /* name is between t_start and paren_open, trimmed */
        const char *name_start = t_start;
        const char *name_end   = paren_open;
        while (name_start < name_end && (*name_start == ' ' || *name_start == '\t'))
            name_start++;
        while (name_end > name_start &&
               (name_end[-1] == ' ' || name_end[-1] == '\t'))
            name_end--;

        if (name_start >= name_end) continue;

        /* section is between paren_open+1 and paren_close */
        const char *sec_start = paren_open + 1;
        const char *sec_end   = paren_close;
        while (sec_start < sec_end && (*sec_start == ' ' || *sec_start == '\t'))
            sec_start++;
        while (sec_end > sec_start &&
               (sec_end[-1] == ' ' || sec_end[-1] == '\t'))
            sec_end--;

        if (sec_start >= sec_end) continue;

        char *name = trim_new(name_start, name_end);
        char *sec  = trim_new(sec_start, sec_end);

        /* Optional: simple filter by section_filter (in case man -k . <section> behaves differently) */
        if (section_filter && strcmp(sec, section_filter) != 0) {
            free(name);
            free(sec);
            continue;
        }

        if (n == cap) {
            cap = cap ? cap * 2 : 32;
            struct name_section *tmp =
                realloc(arr, (size_t)cap * sizeof(*tmp));
            if (!tmp) {
                free(name);
                free(sec);
                free_name_section_array(arr, n);
                free(out);
                die("out of memory in enumerate_manpages");
            }
            arr = tmp;
        }
        arr[n].name    = name;
        arr[n].section = sec;
        n++;
    }

    free(out);
    *out_arr = arr;
    *out_n   = n;
    return 0;
}

/* ---------------- main indexer ---------------- */

static int index_one(sqlite3 *db,
                     const char *name,
                     const char *section)
{
    char *man_txt = read_command_output("man %s %s | col -b", section, name);
    if (!man_txt || man_txt[0] == '\0') {
        fprintf(stderr, "warning: empty man output for %s(%s)\n", name, section);
        free(man_txt);
        return 0; /* not fatal */
    }

    struct parsed_manpage pm;
    memset(&pm, 0, sizeof(pm));
    pm.raw = man_txt;

    parse_sections(pm.raw, &pm);
    extract_name_meta(&pm);
    extract_synopsis_meta(&pm);

    char man_source[64];
    snprintf(man_source, sizeof(man_source), "man %s %s", section, name);

    int func_id = -1;
    if (upsert_function(db, name, section, &pm, man_source, &func_id) != 0) {
        fprintf(stderr, "error: failed to upsert %s(%s)\n", name, section);
        parsed_manpage_free(&pm);
        return -1;
    }

    if (replace_sections(db, func_id, &pm) != 0) {
        fprintf(stderr, "error: failed to store sections for %s(%s)\n", name, section);
        parsed_manpage_free(&pm);
        return -1;
    }

    if (replace_aliases(db, func_id, name, &pm) != 0) {
        fprintf(stderr, "error: failed to store aliases for %s(%s)\n", name, section);
        parsed_manpage_free(&pm);
        return -1;
    }

    parsed_manpage_free(&pm);
    return 0;
}

/* ---------------- CLI ---------------- */

static void usage(const char *prog)
{
    fprintf(stderr,
        "Usage:\n"
        "  %s <db_path> <section> <func1> [func2 ...]\n"
        "      (index explicit functions)\n"
        "\n"
        "  %s <db_path> --scan-section <section>\n"
        "      (index all entries reported by 'man -k . <section>')\n"
        "\n"
        "  %s <db_path> --scan-all\n"
        "      (index all entries reported by 'man -k .')\n",
        prog, prog, prog);
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        usage(argv[0]);
        return EXIT_FAILURE;
    }

    const char *db_path = argv[1];

    sqlite3 *db = NULL;
    if (sqlite3_open(db_path, &db) != SQLITE_OK) {
        fprintf(stderr, "error: cannot open DB %s: %s\n",
                db_path, sqlite3_errmsg(db));
        if (db) sqlite3_close(db);
        return EXIT_FAILURE;
    }

    if (specdb_ensure_schema(db) != 0) {
        sqlite3_close(db);
        return EXIT_FAILURE;
    }

    int rc = 0;

    if (strcmp(argv[2], "--scan-section") == 0) {
        if (argc < 4) {
            usage(argv[0]);
            sqlite3_close(db);
            return EXIT_FAILURE;
        }
        const char *section = argv[3];

        struct name_section *list = NULL;
        int n = 0;
        if (enumerate_manpages(section, &list, &n) != 0) {
            fprintf(stderr, "error: enumeration failed for section %s\n", section);
            sqlite3_close(db);
            return EXIT_FAILURE;
        }

        for (int i = 0; i < n; i++) {
            if (index_one(db, list[i].name, list[i].section) != 0) {
                rc = 1;
            }
        }
        free_name_section_array(list, n);
    } else if (strcmp(argv[2], "--scan-all") == 0) {
        struct name_section *list = NULL;
        int n = 0;
        if (enumerate_manpages(NULL, &list, &n) != 0) {
            fprintf(stderr, "error: enumeration failed for all sections\n");
            sqlite3_close(db);
            return EXIT_FAILURE;
        }

        for (int i = 0; i < n; i++) {
            if (index_one(db, list[i].name, list[i].section) != 0) {
                rc = 1;
            }
        }
        free_name_section_array(list, n);
    } else {
        /* old mode: db section func1 ... */
        if (argc < 4) {
            usage(argv[0]);
            sqlite3_close(db);
            return EXIT_FAILURE;
        }
        const char *section = argv[2];
        for (int i = 3; i < argc; i++) {
            const char *name = argv[i];
            if (index_one(db, name, section) != 0) {
                rc = 1;
            }
        }
    }

    sqlite3_close(db);
    return rc == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

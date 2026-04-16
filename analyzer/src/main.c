#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "mc_ts.h"
#include "mc_rules.h"
#include "mc_db_integration.h"
#include "report.h"
#include "mc_preproc.h"
#include "mc_suppress.h"

static void
print_usage(const char *prog)
{
    fprintf(stderr,
            "Usage: %s [--json | --sarif] [--gcc] [--warn-exit] [--db PATH | --no-db] [--specdb PATH] [--compile-commands PATH] [--dump-views PATH] [--suppressions PATH] <c-file> [c-file...]\n",
            prog);
}

/* Simple JSON string escaper for dump-views output */
static void
json_escape_string(FILE *out, const char *s)
{
    if (!s) {
        fputs("null", out);
        return;
    }

    fputc('"', out);
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        unsigned char c = *p;
        switch (c) {
        case '\\': fputs("\\\\", out); break;
        case '"':  fputs("\\\"", out); break;
        case '\b': fputs("\\b",  out); break;
        case '\f': fputs("\\f",  out); break;
        case '\n': fputs("\\n",  out); break;
        case '\r': fputs("\\r",  out); break;
        case '\t': fputs("\\t",  out); break;
        default:
            if (c < 0x20) {
                fprintf(out, "\\u%04x", c);
            } else {
                fputc((int)c, out);
            }
            break;
        }
    }
    fputc('"', out);
}

struct mc_compile_db_entry {
    char *directory;
    char *file;
    char *resolved_file;
    char *command;
    char **arguments;
    size_t arg_count;
};

struct mc_compile_db {
    struct mc_compile_db_entry *entries;
    size_t count;
};

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

static void
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

static int
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

static const struct mc_compile_db_entry *
mc_find_compile_db_entry(const struct mc_compile_db *db, const char *abs_path)
{
    if (!db || !abs_path)
        return NULL;

    for (size_t i = 0; i < db->count; i++) {
        if (db->entries[i].resolved_file &&
            strcmp(db->entries[i].resolved_file, abs_path) == 0) {
            return &db->entries[i];
        }
    }

    return NULL;
}

/* Per-run context that the preproc hook will use */
struct mc_main_ctx {
    mc_db_ctx *dbctx;
    int json_mode;       /* 0 = text, 1 = JSON */
    int sarif_mode;      /* 0 = off, 1 = SARIF */
    int gcc_mode;        /* 0 = off, 1 = GCC diagnostic format */
    int first_json;      /* for printing commas between JSON files */
    int first_sarif;     /* for printing commas between SARIF results */
    int exit_status;     /* 0 if all good, 1 if any file failed */
    int warn_exit;       /* --warn-exit: exit non-zero on findings */
    FILE *dump_views;    /* NULL = disabled, otherwise JSONL output */
};

/* Hook called by mc_run_preproc_pipeline for each file */
static int
main_on_views(struct mc_preproc_hook *hook,
              const mc_source_views *views)
{
    struct mc_main_ctx *ctx = (struct mc_main_ctx *)hook->user_data;
    const char *path = views->meta.path;

    /* Optional: dump all views to JSONL for training / inspection */
    if (ctx->dump_views) {
        FILE *out = ctx->dump_views;

        /* One JSON object per line:
         * {
         *   "path": "...",
         *   "raw": "...",
         *   "min": "...",
         *   "pp_user": "..."
         * }
         */
        fputc('{', out);

        fputs("\"path\":", out);
        json_escape_string(out, path);
        fputs(",\"raw\":", out);
        json_escape_string(out, views->src_raw);
        fputs(",\"min\":", out);
        json_escape_string(out, views->src_min);
        fputs(",\"pp_user\":", out);
        json_escape_string(out, views->src_pp_user);

        fputs("}\n", out);
    }

    /* Scan raw source for inline suppression markers before analysis. */
    mc_inline_suppress_scan(views->src_raw);

    mc_db_run dbrun;
    memset(&dbrun, 0, sizeof(dbrun));

    mc_db_run_begin(ctx->dbctx, &dbrun, path);
    mc_report_set_db(ctx->dbctx, &dbrun);

    /* Use preprocessed source (_ex variants) when available */
    const char *pp_src = views->src_pp_user;
    size_t pp_len = pp_src ? strlen(pp_src) : 0;
    const unsigned *lmap = views->pp_user_line_map;
    size_t lmap_count = views->pp_user_line_count;

    if (!ctx->json_mode && !ctx->sarif_mode) {
        /* Text mode: warnings only, track issue count in DB run */
        mc_report_reset_run_counters();

        if (!mc_ts_report_unchecked_calls_ex(path, pp_src, pp_len,
                                             lmap, lmap_count)) {
            fprintf(stderr, "analyzer: failed on %s\n", path);
            ctx->exit_status = 1;
        }

        int error_count = (int)mc_report_get_run_issue_count();
        if (error_count > 0 && (ctx->warn_exit || ctx->gcc_mode))
            ctx->exit_status = 1;
        mc_db_run_end(ctx->dbctx, &dbrun, error_count);
    } else if (ctx->json_mode) {
        /* JSON mode: aggregate per-file JSON objects into a files[] array */
        if (!ctx->first_json)
            printf(",\n");
        ctx->first_json = 0;

        mc_report_reset_run_counters();

        if (!mc_ts_report_file_json_ex(path, pp_src, pp_len,
                                       lmap, lmap_count)) {
            fprintf(stderr, "\n/* analyzer: failed on %s */\n", path);
            ctx->exit_status = 1;
        }

        int error_count = (int)mc_report_get_run_issue_count();
        if (error_count > 0 && ctx->warn_exit)
            ctx->exit_status = 1;
        mc_db_run_end(ctx->dbctx, &dbrun, error_count);
    } else {
        mc_report_reset_run_counters();

        if (!mc_ts_report_file_sarif_ex(path, pp_src, pp_len,
                                        lmap, lmap_count,
                                        &ctx->first_sarif)) {
            fprintf(stderr, "analyzer: failed on %s\n", path);
            ctx->exit_status = 1;
            mc_db_run_end(ctx->dbctx, &dbrun, 1);
            mc_inline_suppress_clear();
            return 1;
        }

        int error_count = (int)mc_report_get_run_issue_count();
        if (error_count > 0)
            ctx->exit_status = 1;
        mc_db_run_end(ctx->dbctx, &dbrun, error_count);
    }

    mc_inline_suppress_clear();
    return 0; /* continue pipeline */
}

int
main(int argc, char **argv)
{
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    int json = 0;
    int sarif = 0;
    int gcc = 0;
    int warn_exit = 0;
    const char *db_path = "mancheck.db"; /* default DB path; NULL = disabled */
    const char *specdb_path = NULL;
    const char *compile_commands_path = NULL;
    const char *dump_views_path = NULL;
    const char *suppressions_path = NULL;

    /* First pass: parse options anywhere, collect file args separately */
    char **files = malloc((size_t)(argc - 1) * sizeof(char *));
    if (!files) {
        fprintf(stderr, "out of memory\n");
        return 1;
    }
    size_t file_count = 0;

    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];

        if (strcmp(arg, "--json") == 0) {
            json = 1;
        } else if (strcmp(arg, "--sarif") == 0) {
            sarif = 1;
        } else if (strcmp(arg, "--gcc") == 0) {
            gcc = 1;
        } else if (strcmp(arg, "--warn-exit") == 0) {
            warn_exit = 1;
        } else if (strcmp(arg, "--db") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "%s: --db requires a path argument\n", argv[0]);
                free(files);
                return 1;
            }
            db_path = argv[++i];
        } else if (strcmp(arg, "--no-db") == 0) {
            db_path = NULL;
        } else if (strcmp(arg, "--specdb") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "%s: --specdb requires a path argument\n", argv[0]);
                free(files);
                return 1;
            }
            specdb_path = argv[++i];
        } else if (strcmp(arg, "--compile-commands") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "%s: --compile-commands requires a path argument\n",
                        argv[0]);
                free(files);
                return 1;
            }
            compile_commands_path = argv[++i];
        } else if (strcmp(arg, "--dump-views") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "%s: --dump-views requires a path argument\n",
                        argv[0]);
                free(files);
                return 1;
            }
            dump_views_path = argv[++i];
        } else if (strcmp(arg, "--suppressions") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "%s: --suppressions requires a path argument\n",
                        argv[0]);
                free(files);
                return 1;
            }
            suppressions_path = argv[++i];
        } else if (arg[0] == '-' && arg[1] == '-') {
            fprintf(stderr, "%s: unknown option: %s\n", argv[0], arg);
            free(files);
            return 1;
        } else {
            files[file_count++] = argv[i];
        }
    }

    if (file_count == 0) {
        print_usage(argv[0]);
        free(files);
        return 1;
    }

    if (json && sarif) {
        fprintf(stderr, "%s: --json and --sarif cannot be used together\n",
                argv[0]);
        free(files);
        return 1;
    }

    mc_db_ctx dbctx;
    int rc_db = 0;

    /* Initialize DB context (or disable cleanly) */
    if (db_path != NULL) {
        rc_db = mc_db_ctx_init(&dbctx, db_path);
        if (rc_db != 0) {
            fprintf(stderr,
                    "warning: failed to initialize DB '%s' (rc=%d); "
                    "DB integration disabled for this run\n",
                    db_path, rc_db);
            /* Re-init as disabled context so helpers are safe no-ops */
            mc_db_ctx_init(&dbctx, NULL);
        }
    } else {
        mc_db_ctx_init(&dbctx, NULL);
    }

    /* Optional: load specdb for rule augmentation */
    if (specdb_path) {
        if (mc_rules_init_specdb(specdb_path) != 0) {
            fprintf(stderr,
                    "warning: failed to load specdb '%s'; "
                    "specdb-based rules disabled\n",
                    specdb_path);
        }
    }

    /* Optional: load suppression rules */
    if (suppressions_path) {
        if (mc_suppress_load(suppressions_path) != 0) {
            fprintf(stderr, "error: cannot load suppressions file '%s'\n",
                    suppressions_path);
            mc_db_ctx_close(&dbctx);
            free(files);
            return 1;
        }
    }

    /* Optional: open dump-views JSONL file */
    FILE *dump_views = NULL;
    if (dump_views_path) {
        dump_views = fopen(dump_views_path, "w");
        if (!dump_views) {
            fprintf(stderr, "error: cannot open %s for writing\n",
                    dump_views_path);
            mc_db_ctx_close(&dbctx);
            free(files);
            return 1;
        }
    }

    /* Build mc_file_meta[] for all input files */
    struct mc_compile_db compile_db;
    char *cwd = NULL;
    mc_file_meta *metas = calloc(file_count, sizeof(*metas));
    if (!metas) {
        fprintf(stderr, "out of memory\n");
        if (dump_views)
            (void)fclose(dump_views);
        mc_db_ctx_close(&dbctx);
        free(files);
        return 1;
    }

    memset(&compile_db, 0, sizeof(compile_db));

    cwd = getcwd(NULL, 0);
    if (!cwd) {
        fprintf(stderr, "error: failed to determine current working directory\n");
        free(metas);
        if (dump_views)
            (void)fclose(dump_views);
        mc_suppress_free();
        mc_rules_close_specdb();
        mc_db_ctx_close(&dbctx);
        free(files);
        return 1;
    }

    if (compile_commands_path) {
        if (mc_load_compile_db(compile_commands_path, &compile_db) != 0) {
            fprintf(stderr, "error: cannot load compile_commands.json '%s'\n",
                    compile_commands_path);
            free(cwd);
            free(metas);
            if (dump_views)
                (void)fclose(dump_views);
            mc_suppress_free();
            mc_rules_close_specdb();
            mc_db_ctx_close(&dbctx);
            free(files);
            return 1;
        }
    }

    for (size_t i = 0; i < file_count; i++) {
        const char *path = files[i];
        char *abs_path = mc_resolve_path(cwd, path);
        const struct mc_compile_db_entry *entry;

        if (!abs_path) {
            fprintf(stderr, "out of memory\n");
            for (size_t j = 0; j < i; j++)
                free((char *)metas[j].abs_path);
            mc_compile_db_free(&compile_db);
            free(cwd);
            free(metas);
            if (dump_views)
                (void)fclose(dump_views);
            mc_suppress_free();
            mc_rules_close_specdb();
            mc_db_ctx_close(&dbctx);
            free(files);
            return 1;
        }

        entry = mc_find_compile_db_entry(&compile_db, abs_path);

        metas[i].path        = path;   /* repo-relative if you have it */
        metas[i].abs_path    = abs_path;
        metas[i].language    = "c";
        metas[i].compiler    = "clang";
        metas[i].compile_dir = entry ? entry->directory : NULL;
        metas[i].compile_cmd = entry ? entry->command : NULL;
        metas[i].compile_argv = entry ? (const char *const *)entry->arguments : NULL;
        metas[i].compile_argc = entry ? entry->arg_count : 0;
        metas[i].git_commit  = NULL;
        metas[i].git_status  = NULL;
    }

    free(files);
    free(cwd);

    mc_pp_config cfg = {
        .clang_path  = "clang",
        .extra_flags = "-std=c11"
    };

    struct mc_main_ctx ctx = {
        .dbctx       = &dbctx,
        .json_mode   = json,
        .sarif_mode  = sarif,
        .gcc_mode    = gcc,
        .first_json  = 1,
        .first_sarif = 1,
        .exit_status = 0,
        .warn_exit   = warn_exit,
        .dump_views  = dump_views
    };

    mc_preproc_hook hook = {
        .user_data = &ctx,
        .on_views  = main_on_views,
        .destroy   = NULL
    };

    if (gcc)
        mc_report_set_gcc_mode(1);

    int exit_status = 0;

    if (!json && !sarif) {
        /* Text mode: exactly as before, but via preproc pipeline */
        int rc = mc_run_preproc_pipeline(metas, file_count, &cfg, &hook);
        if (rc != 0) {
            fprintf(stderr, "preprocessing pipeline failed (rc=%d)\n", rc);
            exit_status = 1;
        } else {
            exit_status = ctx.exit_status;
        }
    } else if (json) {
        /* JSON mode: wrap files in a single JSON array */
        printf("{\"files\":[\n");

        int rc = mc_run_preproc_pipeline(metas, file_count, &cfg, &hook);
        if (rc != 0) {
            fprintf(stderr,
                    "\n/* preprocessing pipeline failed (rc=%d) */\n", rc);
            exit_status = 1;
        } else {
            exit_status = ctx.exit_status;
        }

        printf("\n]}\n");
    } else {
        printf("{\"version\":\"2.1.0\",\"runs\":[{\"tool\":{\"driver\":{\"name\":\"mancheck\"}},\"results\":[\n");

        int rc = mc_run_preproc_pipeline(metas, file_count, &cfg, &hook);
        if (rc != 0) {
            fprintf(stderr, "\n/* preprocessing pipeline failed (rc=%d) */\n", rc);
            exit_status = 1;
        } else {
            exit_status = ctx.exit_status;
        }

        printf("\n]}]}\n");
    }

    for (size_t i = 0; i < file_count; i++)
        free((char *)metas[i].abs_path);
    free(metas);
    mc_compile_db_free(&compile_db);
    if (dump_views)
        (void)fclose(dump_views);
    mc_suppress_free();
    mc_rules_close_specdb();
    mc_db_ctx_close(&dbctx);
    return exit_status;
}

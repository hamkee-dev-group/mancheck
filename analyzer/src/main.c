#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "mc_ts.h"
#include "mc_rules.h"
#include "mc_db_integration.h"
#include "report.h"
#include "mc_preproc.h"
#include "mc_compdb.h"
#include "mc_suppress.h"

static void
print_usage(const char *prog)
{
    fprintf(stderr,
            "Usage: %s [--json | --sarif] [--gcc] [--warn-exit] [--db PATH | --no-db] [--specdb PATH] [--compile-commands PATH] [--dump-views PATH] [--suppressions PATH] <c-file> [c-file...]\n",
            prog);
}

static void
print_help(const char *prog)
{
    fprintf(stdout,
        "Usage: %s [--json | --sarif] [--gcc] [--warn-exit] [--db PATH | --no-db] [--specdb PATH] [--compile-commands PATH] [--dump-views PATH] [--suppressions PATH] <c-file> [c-file...]\n"
        "\n"
        "Options:\n"
        "  --json                    Emit JSON diagnostics\n"
        "  --sarif                   Emit SARIF diagnostics\n"
        "  --gcc                     Emit GCC-compatible diagnostic format\n"
        "  --warn-exit               Exit non-zero when findings are reported\n"
        "  --db PATH                 Write runs and facts into the given sqlite DB\n"
        "  --no-db                   Disable DB integration for this run\n"
        "  --specdb PATH             Load specdb for rule augmentation\n"
        "  --compile-commands PATH   Use compile_commands.json for per-file flags\n"
        "  --dump-views PATH         Write per-file preprocessing views as JSONL\n"
        "  --suppressions PATH       Load suppression rules from PATH\n"
        "  -h, --help                Show this help and exit\n"
        "\n"
        "Examples:\n"
        "  ./analyzer/analyzer --sarif --no-db mc_tests/tests/test16_dangerous_functions.c\n"
        "  ./analyzer/analyzer --warn-exit --no-db mc_tests/tests/test30_clean_file.c\n"
        "\n"
        "Exit status:\n"
        "  0  on success (or findings without --warn-exit in text/JSON mode)\n"
        "  1  on errors, or when findings are reported in --warn-exit/--gcc/--sarif modes\n",
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

        if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) {
            print_help(argv[0]);
            free(files);
            return 0;
        } else if (strcmp(arg, "--json") == 0) {
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
        } else if (strcmp(arg, "--compdb") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "%s: --compdb requires a path argument\n",
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
        metas[i].compile_cmd = mc_compdb_lookup(&compile_db, abs_path);
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

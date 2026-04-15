#include <stdio.h>
#include <string.h>

#include "report.h"

/* Current DB context for this run/file. */
static mc_db_ctx *g_dbctx = NULL;
static mc_db_run  g_dbrun;

/* Per-run issue counter (for runs.error_count). */
static unsigned long long g_run_issues = 0;

void mc_report_set_db(mc_db_ctx *ctx, mc_db_run *run)
{
    g_dbctx = ctx;
    if (run) {
        g_dbrun = *run;
    } else {
        memset(&g_dbrun, 0, sizeof(g_dbrun));
    }
}

void mc_report_reset_run_counters(void)
{
    g_run_issues = 0;
}

unsigned long long mc_report_get_run_issue_count(void)
{
    return g_run_issues;
}

/*
 * Generic fact reporter with explicit "kind".
 * This is the main entry point for all new analyses.
 */
void mc_report_fact_kind(const char *file,
                         unsigned line,
                         unsigned column,
                         const char *symbol,
                         const char *kind,
                         const char *msg,
                         struct mc_stats *stats,
                         int quiet)
{
    if (!quiet) {
        printf("%s:%u:%u: %s\n",
               file ? file : "<input>",
               line,
               column,
               msg ? msg : "");
    }
    if (stats) {
        stats->issues_found++;
    }
    g_run_issues++;

    if (g_dbctx && kind && symbol) {
        char details_buf[256];

        if (msg && msg[0] != '\0') {
            snprintf(details_buf, sizeof(details_buf),
                     "col %u: %s", column, msg);
        } else {
            snprintf(details_buf, sizeof(details_buf),
                     "col %u", column);
        }

        mc_db_log_fact(g_dbctx,
                       &g_dbrun,
                       kind,
                       symbol,
                       (int)line,
                       details_buf);
    }
}

/* For ignored/unchecked return of a function call. */
void mc_report_issue(const char *file,
                     unsigned line,
                     unsigned column,
                     const char *func_name,
                     const char *msg,
                     struct mc_stats *stats,
                     int quiet)
{
    if (!quiet) {
        printf("%s:%u:%u: ignored return of %s(): %s\n",
               file ? file : "<input>",
               line,
               column,
               func_name ? func_name : "<func>",
               msg ? msg : "");
    }
    if (stats) {
        stats->issues_found++;
    }
    g_run_issues++;

    /* DB logging: classify as retval_unchecked */
    if (g_dbctx && func_name) {
        char details_buf[256];

        if (msg && msg[0] != '\0') {
            snprintf(details_buf, sizeof(details_buf),
                     "col %u: %s", column, msg);
        } else {
            snprintf(details_buf, sizeof(details_buf),
                     "col %u", column);
        }

        mc_db_log_fact(g_dbctx,
                       &g_dbrun,
                       "retval_unchecked",
                       func_name,
                       (int)line,
                       details_buf);
    }
}

/* Generic warning (dangerous function, format string, etc.). */
void mc_report_warning(const char *file,
                       unsigned line,
                       unsigned column,
                       const char *func_name,
                       const char *msg,
                       struct mc_stats *stats,
                       int quiet)
{
    /* For now, keep legacy behavior: DB kind "warning", symbol = func_name. */
    mc_report_fact_kind(file,
                        line,
                        column,
                        func_name ? func_name : "",
                        "warning",
                        msg,
                        stats,
                        quiet);
}

void mc_report_summary(const struct mc_stats *stats)
{
    printf("mancheck: scanned %llu file(s), found %llu issue(s)\n",
           (unsigned long long)stats->files_scanned,
           (unsigned long long)stats->issues_found);
}
#ifndef MANCHECK_REPORT_H
#define MANCHECK_REPORT_H

#include "mancheck.h"
#include "mc_db_integration.h"

/* Set the current DB context + run for subsequent reports. */
void mc_report_set_db(mc_db_ctx *ctx, mc_db_run *run);

/* Reset and query per-run issue counters (for runs.error_count). */
void mc_report_reset_run_counters(void);
unsigned long long mc_report_get_run_issue_count(void);

/* For "ignored return" / unchecked return issues. */
void mc_report_issue(const char *file,
                     unsigned line,
                     unsigned column,
                     const char *func_name,
                     const char *msg,
                     struct mc_stats *stats,
                     int quiet);

/*
 * Generic fact reporter with an explicit "kind".
 *
 * - file/line/column: source location
 * - symbol: logical subject (function name, variable name, env var name, ...)
 * - kind: analysis kind ("warning", "malloc_size_mismatch", "double_close", ...)
 * - msg: full human-readable message text
 */
void mc_report_fact_kind(const char *file,
                         unsigned line,
                         unsigned column,
                         const char *symbol,
                         const char *kind,
                         const char *msg,
                         struct mc_stats *stats,
                         int quiet);

/* Generic warning reporter (dangerous function, format string, etc.). */
void mc_report_warning(const char *file,
                       unsigned line,
                       unsigned column,
                       const char *func_name,  /* may be NULL */
                       const char *msg,        /* full message text */
                       struct mc_stats *stats, /* may be NULL */
                       int quiet);

void mc_report_summary(const struct mc_stats *stats);

#endif
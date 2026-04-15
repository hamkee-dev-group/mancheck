#ifndef MANCHECK_REPORT_H
#define MANCHECK_REPORT_H

#include "mc_db_integration.h"

/* Enable GCC-compatible output: "file:line:col: warning: ..." for all diags. */
void mc_report_set_gcc_mode(int enabled);

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
                         int quiet);

/* Generic warning reporter (dangerous function, format string, etc.). */
void mc_report_warning(const char *file,
                       unsigned line,
                       unsigned column,
                       const char *func_name,  /* may be NULL */
                       const char *msg,        /* full message text */
                       int quiet);

#endif

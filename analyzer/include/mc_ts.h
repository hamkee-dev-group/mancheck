#ifndef MC_TS_H
#define MC_TS_H

#include <stdbool.h>
#include <stddef.h>
#include <tree_sitter/api.h>

/* Per-file preprocessing context: source + parser + AST */
typedef struct {
    const char *path;
    char *source;
    size_t source_len;
    TSParser *parser;
    TSTree *tree;
    TSNode root;
} mc_ts_file;

typedef enum {
    MC_CALL_STATUS_UNCHECKED = 0,        /* bare call, result not used */
    MC_CALL_STATUS_CHECKED_COND,         /* used in if/while/for/do condition */
    MC_CALL_STATUS_STORED,               /* stored in var/field etc. */
    MC_CALL_STATUS_PROPAGATED,           /* returned to caller */
    MC_CALL_STATUS_IGNORED_EXPLICIT      /* (void)call(...) style */
} mc_call_status;

/* Preprocess (read + parse) one file */
bool mc_ts_file_init(mc_ts_file *f, const char *path);
void mc_ts_file_destroy(mc_ts_file *f);

/* Human-readable warnings (like before, but using improved heuristic) */
bool mc_ts_report_unchecked_calls(const char *path);

/* JSON report for one file:
 *
 * {
 *   "path": "...",
 *   "calls": [
 *     {
 *       "function": "read",
 *       "status": "unchecked",
 *       "category": "return_value_check",
 *       "line": 7,
 *       "column": 5
 *     },
 *     ...
 *   ]
 * }
 */
bool mc_ts_report_file_json(const char *path);

#endif /* MC_TS_H */

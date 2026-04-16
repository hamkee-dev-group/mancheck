#ifndef MC_TS_H
#define MC_TS_H

#include <stdbool.h>
#include <stddef.h>
#include <tree_sitter/api.h>

/* Forward-declare: defined in mc_preproc.h */
struct mc_source_views_tag;
typedef struct mc_source_views_tag mc_source_views_t;

/* Per-file preprocessing context: source + parser + AST */
typedef struct {
    const char *path;
    char *source;
    size_t source_len;
    TSParser *parser;
    TSTree *tree;
    TSNode root;

    /* Optional line map (for preprocessed source).
     * If non-NULL, line_map[tree_sitter_row] = original_file_line. */
    const unsigned *line_map;
    size_t line_map_count;
} mc_ts_file;

typedef enum {
    MC_CALL_STATUS_UNCHECKED = 0,        /* bare call, result not used */
    MC_CALL_STATUS_CHECKED_COND,         /* used in if/while/for/do condition */
    MC_CALL_STATUS_STORED,               /* stored in var/field etc. */
    MC_CALL_STATUS_STORED_UNCHECKED,     /* stored in var, but var never checked */
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

/* Extended versions that use preprocessed source (src_pp_user) when available.
 * These accept the full source views from the preprocessing pipeline, enabling
 * macro-expanded call detection with correct line-number mapping. */
struct mc_source_views_fwd;   /* opaque forward decl; real type in mc_preproc.h */

bool mc_ts_report_unchecked_calls_ex(const char *path,
                                     const char *pp_source,
                                     size_t pp_source_len,
                                     const unsigned *line_map,
                                     size_t line_map_count);

bool mc_ts_report_file_json_ex(const char *path,
                               const char *pp_source,
                               size_t pp_source_len,
                               const unsigned *line_map,
                               size_t line_map_count);

bool mc_ts_report_file_sarif_ex(const char *path,
                                const char *pp_source,
                                size_t pp_source_len,
                                const unsigned *line_map,
                                size_t line_map_count,
                                int *first_result);

#endif /* MC_TS_H */

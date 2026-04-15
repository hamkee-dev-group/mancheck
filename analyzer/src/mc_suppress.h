#ifndef MC_SUPPRESS_H
#define MC_SUPPRESS_H

/* Load suppression rules from a file.  Format: one rule per line,
 *   <path> <category>
 * Lines starting with '#' are comments.  Relative paths in the file
 * are resolved relative to the directory containing the suppressions
 * file itself.  Returns 0 on success, -1 on error. */
int mc_suppress_load(const char *path);

/* Check whether a (file, category) pair is suppressed. */
int mc_suppress_check(const char *file, const char *category);

/* Free all loaded rules. */
void mc_suppress_free(void);

/* Scan raw source for inline suppression markers (// mc:ignore,
 * // NOLINT(mancheck)) and record which original-source lines are
 * suppressed.  Uses a state machine to skip string/char literals and
 * block comments so that markers inside those contexts are ignored.
 * Call before analysis; call mc_inline_suppress_clear() after.
 * Only one file's inline state is active at a time. */
void mc_inline_suppress_scan(const char *src_raw);

/* Check whether original source line `line` (1-based) is suppressed
 * by an inline comment. */
int mc_inline_suppress_check(unsigned line);

/* Free inline suppression state for the current file. */
void mc_inline_suppress_clear(void);

#endif

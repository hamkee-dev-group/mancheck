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

#endif

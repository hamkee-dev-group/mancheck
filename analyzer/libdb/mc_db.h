#ifndef MC_DB_H
#define MC_DB_H

#include <stddef.h>

typedef struct sqlite3 sqlite3;

typedef struct {
    sqlite3 *db;
} mc_db;

/* Open or create the database at path */
int mc_db_open(mc_db *m, const char *path);

/* Close database (safe to call on already-closed) */
void mc_db_close(mc_db *m);

/* Ensure schema exists (tables, triggers, FTS) */
int mc_db_init_schema(mc_db *m);

/* Start a new analysis run, returns run_id via out param */
int mc_db_begin_run(mc_db *m, const char *filename, long *run_id);

/* Finish a run, updating error_count */
int mc_db_end_run(mc_db *m, long run_id, int error_count);

/* Insert a fact (warning, call, pattern, error, etc.) */
int mc_db_insert_fact(mc_db *m,
                      long run_id,
                      const char *kind,   /* e.g. "warning", "call" */
                      const char *symbol, /* may be NULL */
                      int line,           /* 0 if not applicable */
                      const char *details /* may be NULL */);

/*
 * Simple similarity / lookup helper.
 * Performs an FTS search on symbol/details.
 * cb may be NULL to ignore results.
 */
int mc_db_find_similar(mc_db *m,
                       const char *query,
                       void (*cb)(long run_id,
                                  const char *filename,
                                  const char *symbol,
                                  const char *details));

#endif /* MC_DB_H */

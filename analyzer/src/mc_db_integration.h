#ifndef MC_DB_INTEGRATION_H
#define MC_DB_INTEGRATION_H

#include "mc_db.h"

/* High-level context for one process invocation */
typedef struct mc_db_ctx {
    mc_db db;
    int   enabled;     /* 0 = no DB, 1 = DB active */
} mc_db_ctx;

/* Per-file analysis context */
typedef struct mc_db_run {
    long run_id;       /* 0 = no active run */
} mc_db_run;

/*
 * Open DB at path and ensure schema exists.
 * If path is NULL, this will disable DB (enabled=0).
 */
int mc_db_ctx_init(mc_db_ctx *ctx, const char *path);

/* Close DB (if enabled) */
void mc_db_ctx_close(mc_db_ctx *ctx);

/* Start a new run (per source file) */
int mc_db_run_begin(mc_db_ctx *ctx,
                    mc_db_run *run,
                    const char *filename);

/* Finish a run and store final error_count */
int mc_db_run_end(mc_db_ctx *ctx,
                  mc_db_run *run,
                  int error_count);

/*
 * Generic fact logger: inserts a row with the given kind.
 * No-op if DB is disabled or run_id == 0.
 */
void mc_db_log_fact(mc_db_ctx *ctx,
                    mc_db_run *run,
                    const char *kind,
                    const char *symbol,
                    int line,
                    const char *details);

/*
 * Backwards-compatible helper: kind = "warning".
 */
void mc_db_log_warning(mc_db_ctx *ctx,
                       mc_db_run *run,
                       const char *symbol,
                       int line,
                       const char *details);

#endif /* MC_DB_INTEGRATION_H */

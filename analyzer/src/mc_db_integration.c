#include "mc_db_integration.h"

#include <stdio.h>
#include <string.h>

int mc_db_ctx_init(mc_db_ctx *ctx, const char *path)
{
    if (!ctx) {
        return 0;
    }

    memset(ctx, 0, sizeof(*ctx));

    if (!path || path[0] == '\0') {
        ctx->enabled = 0;
        return 0;
    }

    int rc = mc_db_open(&ctx->db, path);
    if (rc != 0) {
        fprintf(stderr, "mc_db_ctx_init: failed to open DB '%s' (rc=%d)\n",
                path, rc);
        ctx->enabled = 0;
        return rc;
    }

    rc = mc_db_init_schema(&ctx->db);
    if (rc != 0) {
        fprintf(stderr, "mc_db_ctx_init: failed to init schema (rc=%d)\n", rc);
        mc_db_close(&ctx->db);
        ctx->enabled = 0;
        return rc;
    }

    ctx->enabled = 1;
    return 0;
}

void mc_db_ctx_close(mc_db_ctx *ctx)
{
    if (!ctx) {
        return;
    }
    if (ctx->enabled) {
        mc_db_close(&ctx->db);
    }
    memset(ctx, 0, sizeof(*ctx));
}

int mc_db_run_begin(mc_db_ctx *ctx,
                    mc_db_run *run,
                    const char *filename)
{
    if (!ctx || !run) {
        return 0;
    }

    memset(run, 0, sizeof(*run));

    if (!ctx->enabled) {
        return 0;
    }

    long run_id = 0;
    int rc = mc_db_begin_run(&ctx->db, filename, &run_id);
    if (rc != 0) {
        fprintf(stderr, "mc_db_run_begin: mc_db_begin_run failed (rc=%d)\n", rc);
        return rc;
    }

    run->run_id = run_id;
    return 0;
}

int mc_db_run_end(mc_db_ctx *ctx,
                  mc_db_run *run,
                  int error_count)
{
    if (!ctx || !run) {
        return 0;
    }

    if (!ctx->enabled || run->run_id <= 0) {
        return 0;
    }

    int rc = mc_db_end_run(&ctx->db, run->run_id, error_count);
    if (rc != 0) {
        fprintf(stderr, "mc_db_run_end: mc_db_end_run failed (rc=%d)\n", rc);
    }

    run->run_id = 0;
    return rc;
}

void mc_db_log_fact(mc_db_ctx *ctx,
                    mc_db_run *run,
                    const char *kind,
                    const char *symbol,
                    int line,
                    const char *details)
{
    if (!ctx || !run) {
        return;
    }
    if (!ctx->enabled || run->run_id <= 0 || !kind) {
        return;
    }

    mc_db_insert_fact(&ctx->db,
                      run->run_id,
                      kind,
                      symbol,
                      line,
                      details);
}

void mc_db_log_warning(mc_db_ctx *ctx,
                       mc_db_run *run,
                       const char *symbol,
                       int line,
                       const char *details)
{
    mc_db_log_fact(ctx, run, "warning", symbol, line, details);
}

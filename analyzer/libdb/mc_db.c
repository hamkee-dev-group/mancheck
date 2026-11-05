#include "mc_db.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <sqlite3.h>

/*
 * Schema versions (PRAGMA user_version):
 *   0  empty database, or one written before versioning (runs/facts/FTS only)
 *   1  adds runs.status: 'started' | 'completed' | 'failed'
 */
#define MC_DB_SCHEMA_VERSION 1

/* Every statement is idempotent so an interrupted or legacy schema can be
 * completed inside the same transaction that sets the version. */
static const char *mc_db_schema_sql =
    "CREATE TABLE IF NOT EXISTS runs (\n"
    "  id INTEGER PRIMARY KEY AUTOINCREMENT,\n"
    "  filename TEXT NOT NULL,\n"
    "  ts DATETIME DEFAULT CURRENT_TIMESTAMP,\n"
    "  error_count INTEGER DEFAULT 0,\n"
    "  status TEXT NOT NULL DEFAULT 'started'\n"
    ");\n"
    "CREATE TABLE IF NOT EXISTS facts (\n"
    "  id INTEGER PRIMARY KEY AUTOINCREMENT,\n"
    "  run_id INTEGER NOT NULL,\n"
    "  kind TEXT NOT NULL,\n"
    "  symbol TEXT,\n"
    "  line INTEGER,\n"
    "  details TEXT,\n"
    "  FOREIGN KEY(run_id) REFERENCES runs(id)\n"
    ");\n"
    "CREATE VIRTUAL TABLE IF NOT EXISTS facts_fts USING fts5(\n"
    "  symbol,\n"
    "  details,\n"
    "  content='facts',\n"
    "  content_rowid='id'\n"
    ");\n"
    "CREATE TRIGGER IF NOT EXISTS facts_ai AFTER INSERT ON facts BEGIN\n"
    "  INSERT INTO facts_fts(rowid, symbol, details)\n"
    "  VALUES (new.id, new.symbol, new.details);\n"
    "END;\n"
    "CREATE TRIGGER IF NOT EXISTS facts_ad AFTER DELETE ON facts BEGIN\n"
    "  INSERT INTO facts_fts(facts_fts, rowid, symbol, details)\n"
    "  VALUES('delete', old.id, old.symbol, old.details);\n"
    "END;\n"
    "CREATE TRIGGER IF NOT EXISTS facts_au AFTER UPDATE ON facts BEGIN\n"
    "  INSERT INTO facts_fts(facts_fts, rowid, symbol, details)\n"
    "  VALUES('delete', old.id, old.symbol, old.details);\n"
    "  INSERT INTO facts_fts(rowid, symbol, details)\n"
    "  VALUES(new.id, new.symbol, new.details);\n"
    "END;\n";

int mc_db_open(mc_db *m, const char *path)
{
    if (!m || !path) {
        return SQLITE_MISUSE;
    }

    memset(m, 0, sizeof(*m));

    int rc = sqlite3_open(path, &m->db);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "mc_db_open: cannot open '%s': %s\n",
                path, sqlite3_errmsg(m->db));
        sqlite3_close(m->db);
        m->db = NULL;
        return rc;
    }

    /* Journal mode WAL is good default for concurrent readers */
    char *errmsg = NULL;
    rc = sqlite3_exec(m->db, "PRAGMA journal_mode=WAL;",
                      NULL, NULL, &errmsg);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "mc_db_open: pragma journal_mode=WAL failed: %s\n",
                errmsg ? errmsg : "(unknown)");
        sqlite3_free(errmsg);
        /* Not fatal; continue */
        rc = SQLITE_OK;
    }

    /* Ensure foreign keys are enabled */
    errmsg = NULL;
    int rc2 = sqlite3_exec(m->db, "PRAGMA foreign_keys=ON;",
                           NULL, NULL, &errmsg);
    if (rc2 != SQLITE_OK) {
        fprintf(stderr, "mc_db_open: pragma foreign_keys=ON failed: %s\n",
                errmsg ? errmsg : "(unknown)");
        sqlite3_free(errmsg);
        /* Also not fatal here */
    }

    return rc;
}

void mc_db_close(mc_db *m)
{
    if (!m || !m->db) {
        return;
    }
    sqlite3_close(m->db);
    m->db = NULL;
}

static int mc_db_exec(mc_db *m, const char *sql, const char *what)
{
    char *errmsg = NULL;
    int rc = sqlite3_exec(m->db, sql, NULL, NULL, &errmsg);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "mc_db_init_schema: %s failed: %s\n",
                what, errmsg ? errmsg : sqlite3_errmsg(m->db));
    }
    sqlite3_free(errmsg);
    return rc;
}

static int mc_db_query_int(mc_db *m, const char *sql, const char *arg, int *out)
{
    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(m->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        return rc;
    }
    if (arg) {
        sqlite3_bind_text(stmt, 1, arg, -1, SQLITE_STATIC);
    }
    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        *out = sqlite3_column_int(stmt, 0);
        rc = SQLITE_OK;
    } else if (rc == SQLITE_DONE) {
        *out = 0;
        rc = SQLITE_OK;
    }
    sqlite3_finalize(stmt);
    return rc;
}

/* 1 if table has the column, 0 if not, -1 on query failure. */
static int mc_db_has_column(mc_db *m, const char *table, const char *column)
{
    char sql[128];
    snprintf(sql, sizeof(sql),
             "SELECT 1 FROM pragma_table_info('%s') WHERE name=?1;", table);
    int found = 0;
    if (mc_db_query_int(m, sql, column, &found) != SQLITE_OK) {
        return -1;
    }
    return found;
}

/* Existing tables must carry the columns mancheck reads and writes; anything
 * else is a different application's database and must not be modified. */
static int mc_db_check_columns(mc_db *m, const char *table,
                               const char *const *columns)
{
    int exists = 0;
    if (mc_db_query_int(m, "SELECT count(*) FROM sqlite_master "
                           "WHERE type='table' AND name=?1;",
                        table, &exists) != SQLITE_OK) {
        return SQLITE_ERROR;
    }
    if (!exists) {
        return SQLITE_OK;
    }
    for (const char *const *c = columns; *c; c++) {
        if (mc_db_has_column(m, table, *c) != 1) {
            fprintf(stderr, "mc_db_init_schema: table '%s' has no column '%s'; "
                            "not a mancheck results database\n", table, *c);
            return SQLITE_ERROR;
        }
    }
    return SQLITE_OK;
}

int mc_db_init_schema(mc_db *m)
{
    if (!m || !m->db) {
        return SQLITE_MISUSE;
    }

    static const char *const run_cols[] = { "id", "filename", "ts", "error_count", NULL };
    static const char *const fact_cols[] = { "id", "run_id", "kind", "symbol", "line", "details", NULL };

    /* IMMEDIATE: concurrent analyzers serialize here instead of racing to
     * create or migrate the same schema. */
    int rc = mc_db_exec(m, "BEGIN IMMEDIATE;", "begin");
    if (rc != SQLITE_OK) {
        return rc;
    }

    int version = 0;
    rc = mc_db_query_int(m, "PRAGMA user_version;", NULL, &version);
    if (rc != SQLITE_OK) {
        goto fail;
    }
    if (version > MC_DB_SCHEMA_VERSION) {
        fprintf(stderr, "mc_db_init_schema: database schema version %d is newer "
                        "than supported version %d\n", version, MC_DB_SCHEMA_VERSION);
        rc = SQLITE_ERROR;
        goto fail;
    }

    rc = mc_db_check_columns(m, "runs", run_cols);
    if (rc == SQLITE_OK) {
        rc = mc_db_check_columns(m, "facts", fact_cols);
    }
    if (rc != SQLITE_OK) {
        goto fail;
    }

    rc = mc_db_exec(m, mc_db_schema_sql, "create schema");
    if (rc != SQLITE_OK) {
        goto fail;
    }

    int has_status = mc_db_has_column(m, "runs", "status");
    if (has_status < 0) {
        rc = SQLITE_ERROR;
        goto fail;
    }
    if (!has_status) {
        /* Version 0 runs predate run state; their outcome is unknown. */
        rc = mc_db_exec(m, "ALTER TABLE runs ADD COLUMN status TEXT NOT NULL DEFAULT 'started';",
                        "add runs.status");
        if (rc != SQLITE_OK) {
            goto fail;
        }
    }

    if (version != MC_DB_SCHEMA_VERSION) {
        char sql[64];
        snprintf(sql, sizeof(sql), "PRAGMA user_version=%d;", MC_DB_SCHEMA_VERSION);
        rc = mc_db_exec(m, sql, "set schema version");
        if (rc != SQLITE_OK) {
            goto fail;
        }
    }

    return mc_db_exec(m, "COMMIT;", "commit");

fail:
    sqlite3_exec(m->db, "ROLLBACK;", NULL, NULL, NULL);
    return rc;
}

int mc_db_begin_run(mc_db *m, const char *filename, long *run_id)
{
    if (!m || !m->db || !filename || !run_id) {
        return SQLITE_MISUSE;
    }

    const char *sql = "INSERT INTO runs(filename) VALUES (?1);";
    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(m->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "mc_db_begin_run: prepare failed: %s\n",
                sqlite3_errmsg(m->db));
        return rc;
    }

    sqlite3_bind_text(stmt, 1, filename, -1, SQLITE_TRANSIENT);

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        fprintf(stderr, "mc_db_begin_run: step failed: %s\n",
                sqlite3_errmsg(m->db));
        sqlite3_finalize(stmt);
        return rc;
    }

    sqlite3_finalize(stmt);

    sqlite3_int64 rowid = sqlite3_last_insert_rowid(m->db);
    *run_id = (long)rowid;

    return SQLITE_OK;
}

int mc_db_end_run(mc_db *m, long run_id, int error_count, int completed)
{
    if (!m || !m->db || run_id <= 0) {
        return SQLITE_MISUSE;
    }

    const char *sql = "UPDATE runs SET error_count=?1, status=?3 WHERE id=?2;";
    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(m->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "mc_db_end_run: prepare failed: %s\n",
                sqlite3_errmsg(m->db));
        return rc;
    }

    sqlite3_bind_int(stmt, 1, error_count);
    sqlite3_bind_int64(stmt, 2, (sqlite3_int64)run_id);
    sqlite3_bind_text(stmt, 3, completed ? "completed" : "failed", -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt);
    if (rc == SQLITE_DONE && sqlite3_changes(m->db) != 1) {
        fprintf(stderr, "mc_db_end_run: run %ld no longer exists\n", run_id);
        sqlite3_finalize(stmt);
        return SQLITE_NOTFOUND;
    }
    if (rc != SQLITE_DONE) {
        fprintf(stderr, "mc_db_end_run: step failed: %s\n",
                sqlite3_errmsg(m->db));
        sqlite3_finalize(stmt);
        return rc;
    }

    sqlite3_finalize(stmt);
    return SQLITE_OK;
}

int mc_db_insert_fact(mc_db *m,
                      long run_id,
                      const char *kind,
                      const char *symbol,
                      int line,
                      const char *details)
{
    if (!m || !m->db || !kind || run_id <= 0) {
        return SQLITE_MISUSE;
    }

    const char *sql =
        "INSERT INTO facts(run_id, kind, symbol, line, details)\n"
        "VALUES (?1, ?2, ?3, ?4, ?5);";

    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(m->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "mc_db_insert_fact: prepare failed: %s\n",
                sqlite3_errmsg(m->db));
        return rc;
    }

    sqlite3_bind_int64(stmt, 1, (sqlite3_int64)run_id);
    sqlite3_bind_text(stmt, 2, kind, -1, SQLITE_TRANSIENT);

    if (symbol) {
        sqlite3_bind_text(stmt, 3, symbol, -1, SQLITE_TRANSIENT);
    } else {
        sqlite3_bind_null(stmt, 3);
    }

    sqlite3_bind_int(stmt, 4, line);

    if (details) {
        sqlite3_bind_text(stmt, 5, details, -1, SQLITE_TRANSIENT);
    } else {
        sqlite3_bind_null(stmt, 5);
    }

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        fprintf(stderr, "mc_db_insert_fact: step failed: %s\n",
                sqlite3_errmsg(m->db));
        sqlite3_finalize(stmt);
        return rc;
    }

    sqlite3_finalize(stmt);
    return SQLITE_OK;
}

int mc_db_find_similar(mc_db *m,
                       const char *query,
                       void (*cb)(long run_id,
                                  const char *filename,
                                  const char *symbol,
                                  const char *details))
{
    if (!m || !m->db || !query) {
        return SQLITE_MISUSE;
    }

    const char *sql =
        "SELECT runs.id, runs.filename, facts.symbol, facts.details\n"
        "FROM facts_fts\n"
        "JOIN facts ON facts_fts.rowid = facts.id\n"
        "JOIN runs ON facts.run_id = runs.id\n"
        "WHERE facts_fts MATCH ?1\n"
        "ORDER BY runs.ts DESC\n"
        "LIMIT 32;";

    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(m->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "mc_db_find_similar: prepare failed: %s\n",
                sqlite3_errmsg(m->db));
        return rc;
    }

    sqlite3_bind_text(stmt, 1, query, -1, SQLITE_TRANSIENT);

    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        long run_id = (long)sqlite3_column_int64(stmt, 0);
        const char *filename = (const char *)sqlite3_column_text(stmt, 1);
        const char *symbol   = (const char *)sqlite3_column_text(stmt, 2);
        const char *details  = (const char *)sqlite3_column_text(stmt, 3);

        if (cb) {
            cb(run_id,
               filename ? filename : "",
               symbol ? symbol : "",
               details ? details : "");
        }
    }

    if (rc != SQLITE_DONE) {
        fprintf(stderr, "mc_db_find_similar: step failed: %s\n",
                sqlite3_errmsg(m->db));
        sqlite3_finalize(stmt);
        return rc;
    }

    sqlite3_finalize(stmt);
    return SQLITE_OK;
}

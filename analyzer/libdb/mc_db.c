#include "mc_db.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <sqlite3.h>

static const char *mc_db_schema_sql =
    "PRAGMA foreign_keys = ON;\n"
    "CREATE TABLE IF NOT EXISTS runs (\n"
    "  id INTEGER PRIMARY KEY AUTOINCREMENT,\n"
    "  filename TEXT NOT NULL,\n"
    "  ts DATETIME DEFAULT CURRENT_TIMESTAMP,\n"
    "  error_count INTEGER DEFAULT 0\n"
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

static int mc_db_table_exists(mc_db *m, const char *name)
{
    const char *sql =
        "SELECT 1 FROM sqlite_master WHERE type='table' AND name=?1 LIMIT 1;";
    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(m->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        return 0;
    }

    sqlite3_bind_text(stmt, 1, name, -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt);
    int exists = (rc == SQLITE_ROW);

    sqlite3_finalize(stmt);
    return exists;
}

int mc_db_init_schema(mc_db *m)
{
    if (!m || !m->db) {
        return SQLITE_MISUSE;
    }

    /* If 'runs' already exists, assume schema is in place */
    if (mc_db_table_exists(m, "runs")) {
        return SQLITE_OK;
    }

    char *errmsg = NULL;
    int rc = sqlite3_exec(m->db, mc_db_schema_sql, NULL, NULL, &errmsg);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "mc_db_init_schema: failed: %s\n",
                errmsg ? errmsg : "(unknown)");
        sqlite3_free(errmsg);
    }
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

int mc_db_end_run(mc_db *m, long run_id, int error_count)
{
    if (!m || !m->db || run_id <= 0) {
        return SQLITE_MISUSE;
    }

    const char *sql = "UPDATE runs SET error_count=?1 WHERE id=?2;";
    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(m->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "mc_db_end_run: prepare failed: %s\n",
                sqlite3_errmsg(m->db));
        return rc;
    }

    sqlite3_bind_int(stmt, 1, error_count);
    sqlite3_bind_int64(stmt, 2, (sqlite3_int64)run_id);

    rc = sqlite3_step(stmt);
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

// specdb/src/specdb.c
#define _XOPEN_SOURCE 700
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "specdb.h"

static const char *SCHEMA_SQL =
    "PRAGMA foreign_keys=ON;"
    "CREATE TABLE IF NOT EXISTS functions ("
    "  id          INTEGER PRIMARY KEY AUTOINCREMENT,"
    "  name        TEXT NOT NULL,"
    "  section     TEXT NOT NULL,"
    "  short_name  TEXT,"
    "  short_desc  TEXT,"
    "  header      TEXT,"
    "  proto       TEXT,"
    "  man_source  TEXT,"
    "  raw         TEXT"
    ");"
    "CREATE UNIQUE INDEX IF NOT EXISTS idx_functions_name_section "
    "  ON functions(name, section);"
    "CREATE TABLE IF NOT EXISTS function_sections ("
    "  id           INTEGER PRIMARY KEY AUTOINCREMENT,"
    "  function_id  INTEGER NOT NULL,"
    "  section_name TEXT NOT NULL,"
    "  content      TEXT NOT NULL,"
    "  ord          INTEGER NOT NULL,"
    "  FOREIGN KEY(function_id) REFERENCES functions(id) ON DELETE CASCADE"
    ");"
    "CREATE INDEX IF NOT EXISTS idx_function_sections_funcid "
    "  ON function_sections(function_id, ord);"
    "CREATE TABLE IF NOT EXISTS function_aliases ("
    "  id           INTEGER PRIMARY KEY AUTOINCREMENT,"
    "  function_id  INTEGER NOT NULL,"
    "  alias_name   TEXT NOT NULL,"
    "  FOREIGN KEY(function_id) REFERENCES functions(id) ON DELETE CASCADE"
    ");"
    "CREATE UNIQUE INDEX IF NOT EXISTS idx_function_aliases_name_func "
    "  ON function_aliases(alias_name, function_id);";

int specdb_ensure_schema(sqlite3 *db)
{
    char *errmsg = NULL;
    if (sqlite3_exec(db, SCHEMA_SQL, NULL, NULL, &errmsg) != SQLITE_OK) {
        fprintf(stderr, "specdb: schema error: %s\n", errmsg ? errmsg : "(unknown)");
        sqlite3_free(errmsg);
        return -1;
    }
    return 0;
}

int specdb_open(const char *path, sqlite3 **db_out)
{
    if (!db_out) {
        return -1;
    }
    *db_out = NULL;

    sqlite3 *db = NULL;
    if (sqlite3_open(path, &db) != SQLITE_OK) {
        fprintf(stderr, "specdb: cannot open %s: %s\n", path, sqlite3_errmsg(db));
        if (db) sqlite3_close(db);
        return -1;
    }

    if (specdb_ensure_schema(db) != 0) {
        sqlite3_close(db);
        return -1;
    }

    *db_out = db;
    return 0;
}

void specdb_close(sqlite3 *db)
{
    if (db) {
        sqlite3_close(db);
    }
}

static char *dup_or_null(const unsigned char *s)
{
    if (!s) return NULL;
    size_t len = strlen((const char *)s);
    char *out = malloc(len + 1);
    if (!out) return NULL;
    memcpy(out, s, len + 1);
    return out;
}

static void specdb_init_func(struct specdb_func *f)
{
    if (!f) return;
    memset(f, 0, sizeof(*f));
}

void specdb_free_function(struct specdb_func *f)
{
    if (!f) return;
    free(f->name);
    free(f->section);
    free(f->short_name);
    free(f->short_desc);
    free(f->header);
    free(f->proto);
    free(f->man_source);
    free(f->raw);

    if (f->sections) {
        for (int i = 0; i < f->n_sections; i++) {
            free(f->sections[i].name);
            free(f->sections[i].content);
        }
        free(f->sections);
    }
    specdb_init_func(f);
}

/* internal helper: load sections for a given function id into out */
static int load_sections(sqlite3 *db, int function_id, struct specdb_func *out)
{
    const char *sql_sec =
        "SELECT section_name, content, ord "
        "FROM function_sections WHERE function_id = ?1 ORDER BY ord ASC;";

    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, sql_sec, -1, &stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "specdb: prepare sections error: %s\n", sqlite3_errmsg(db));
        return -1;
    }

    sqlite3_bind_int(stmt, 1, function_id);

    struct specdb_section *sections = NULL;
    int nsec = 0;
    int cap  = 0;

    int rc;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        if (nsec == cap) {
            cap = cap ? cap * 2 : 4;
            struct specdb_section *tmp =
                realloc(sections, (size_t)cap * sizeof(*tmp));
            if (!tmp) {
                fprintf(stderr, "specdb: out of memory\n");
                sqlite3_finalize(stmt);
                free(sections);
                return -1;
            }
            sections = tmp;
        }
        struct specdb_section *s = &sections[nsec];
        memset(s, 0, sizeof(*s));
        s->name        = dup_or_null(sqlite3_column_text(stmt, 0));
        s->content     = dup_or_null(sqlite3_column_text(stmt, 1));
        s->order_index = sqlite3_column_int(stmt, 2);
        nsec++;
    }

    if (rc != SQLITE_DONE) {
        fprintf(stderr, "specdb: sections step error: %s\n", sqlite3_errmsg(db));
        sqlite3_finalize(stmt);
        if (sections) {
            for (int i = 0; i < nsec; i++) {
                free(sections[i].name);
                free(sections[i].content);
            }
            free(sections);
        }
        return -1;
    }

    sqlite3_finalize(stmt);
    out->sections   = sections;
    out->n_sections = nsec;
    return 0;
}

/* internal helper: load main function row by function id into out */
static int load_function_by_id(sqlite3 *db, int function_id, struct specdb_func *out)
{
    const char *sql =
        "SELECT id, name, section, short_name, short_desc, header, proto, man_source, raw "
        "FROM functions WHERE id = ?1;";

    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "specdb: prepare error: %s\n", sqlite3_errmsg(db));
        return -1;
    }

    sqlite3_bind_int(stmt, 1, function_id);

    int rc = sqlite3_step(stmt);
    if (rc == SQLITE_DONE) {
        sqlite3_finalize(stmt);
        return 1; /* not found */
    } else if (rc != SQLITE_ROW) {
        fprintf(stderr, "specdb: step error: %s\n", sqlite3_errmsg(db));
        sqlite3_finalize(stmt);
        return -1;
    }

    int col = 0;
    (void)sqlite3_column_int(stmt, col++); /* id, already known */
    out->name       = dup_or_null(sqlite3_column_text(stmt, col++));
    out->section    = dup_or_null(sqlite3_column_text(stmt, col++));
    out->short_name = dup_or_null(sqlite3_column_text(stmt, col++));
    out->short_desc = dup_or_null(sqlite3_column_text(stmt, col++));
    out->header     = dup_or_null(sqlite3_column_text(stmt, col++));
    out->proto      = dup_or_null(sqlite3_column_text(stmt, col++));
    out->man_source = dup_or_null(sqlite3_column_text(stmt, col++));
    out->raw        = dup_or_null(sqlite3_column_text(stmt, col++));

    sqlite3_finalize(stmt);

    if (load_sections(db, function_id, out) != 0) {
        specdb_free_function(out);
        return -1;
    }

    return 0;
}

int specdb_lookup_function(sqlite3 *db,
                           const char *name,
                           const char *section,
                           struct specdb_func *out)
{
    if (!db || !name || !section || !out) return -1;

    specdb_init_func(out);

    /* 1) try direct match in functions(name, section) */
    const char *sql_func =
        "SELECT id FROM functions WHERE name = ?1 AND section = ?2;";

    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, sql_func, -1, &stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "specdb: prepare error: %s\n", sqlite3_errmsg(db));
        return -1;
    }

    sqlite3_bind_text(stmt, 1, name,    -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, section, -1, SQLITE_STATIC);

    int rc = sqlite3_step(stmt);
    int function_id = -1;

    if (rc == SQLITE_ROW) {
        function_id = sqlite3_column_int(stmt, 0);
        sqlite3_finalize(stmt);
        return load_function_by_id(db, function_id, out);
    } else if (rc != SQLITE_DONE) {
        fprintf(stderr, "specdb: step error: %s\n", sqlite3_errmsg(db));
        sqlite3_finalize(stmt);
        return -1;
    }
    sqlite3_finalize(stmt);

    /* 2) fall back to alias lookup */
    const char *sql_alias =
        "SELECT f.id "
        "FROM functions f "
        "JOIN function_aliases a ON a.function_id = f.id "
        "WHERE a.alias_name = ?1 AND f.section = ?2 "
        "LIMIT 1;";

    if (sqlite3_prepare_v2(db, sql_alias, -1, &stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "specdb: prepare alias error: %s\n", sqlite3_errmsg(db));
        return -1;
    }

    sqlite3_bind_text(stmt, 1, name,    -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, section, -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        function_id = sqlite3_column_int(stmt, 0);
        sqlite3_finalize(stmt);
        return load_function_by_id(db, function_id, out);
    } else if (rc == SQLITE_DONE) {
        sqlite3_finalize(stmt);
        return 1; /* not found at all */
    } else {
        fprintf(stderr, "specdb: alias step error: %s\n", sqlite3_errmsg(db));
        sqlite3_finalize(stmt);
        return -1;
    }
}

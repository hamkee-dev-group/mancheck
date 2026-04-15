#ifndef SPECDB_H
#define SPECDB_H

#include <sqlite3.h>

#ifdef __cplusplus
extern "C" {
#endif

struct specdb_section {
    char *name;        /* e.g., "DESCRIPTION", "RETURN VALUE" */
    char *content;     /* full text of that section */
    int   order_index; /* 0-based order in manpage */
};

struct specdb_func {
    char *name;        /* "read" */
    char *section;     /* "2" */
    char *short_name;  /* left side of NAME, e.g. "read - ..." */
    char *short_desc;  /* right side of NAME line */
    char *header;      /* e.g. "<unistd.h>" if parsed */
    char *proto;       /* first prototype found in SYNOPSIS (best-effort) */
    char *man_source;  /* e.g. "man 2 read" */
    char *raw;         /* full man output (col -b) */

    struct specdb_section *sections;
    int n_sections;
};

/* Open DB and ensure schema exists. Returns 0 on success. */
int specdb_open(const char *path, sqlite3 **db_out);

/* Close DB (just a thin wrapper). */
void specdb_close(sqlite3 *db);

/* Look up a function by name + section. Returns 0 on success, >0 if not found, <0 on error. */
int specdb_lookup_function(sqlite3 *db,
                           const char *name,
                           const char *section,
                           struct specdb_func *out);

/* Free everything inside specdb_func. */
void specdb_free_function(struct specdb_func *f);

/* Ensure schema on an already-open DB. Returns 0 on success. */
int specdb_ensure_schema(sqlite3 *db);

#ifdef __cplusplus
}
#endif

#endif /* SPECDB_H */

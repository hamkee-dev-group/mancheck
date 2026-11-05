#ifndef SPECDB_H
#define SPECDB_H

#include <sqlite3.h>
#include <stddef.h>

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

/*
 * Check whether a function (by name, any section) has a "RETURN VALUE"
 * section in its manpage.  Tries sections 2, 3, then any other.
 *
 * Returns:  1  = yes, has RETURN VALUE
 *           0  = found but no RETURN VALUE section (or not found at all)
 *          -1  = database error
 */
int specdb_function_has_retval(sqlite3 *db, const char *name);

/*
 * Check whether a function's manpage describes it as deprecated/unsafe.
 * Searches DESCRIPTION, NOTES, BUGS, CAVEATS, WARNINGS sections for
 * keywords like "deprecated", "obsolete", "should not be used", etc.
 *
 * Returns:  1 = yes, dangerous/deprecated
 *           0 = not found or no such keywords
 *          -1 = database error
 */
int specdb_function_is_dangerous(sqlite3 *db, const char *name);

/*
 * Check whether a function's prototype looks like a printf/scanf-family
 * format-string function (variadic with a format/fmt parameter).
 *
 * Returns:  1 = yes, likely format-string function
 *           0 = not found or no format pattern
 *          -1 = database error
 */
int specdb_function_has_format_string(sqlite3 *db, const char *name);

/* Evidence-backed contracts. UNKNOWN is deliberately not a must-check rule. */
enum specdb_failure {
    SPECDB_FAILURE_UNKNOWN, SPECDB_FAILURE_NONE, SPECDB_FAILURE_NEGATIVE,
    SPECDB_FAILURE_NULL, SPECDB_FAILURE_MAP_FAILED, SPECDB_FAILURE_NONZERO,
    SPECDB_FAILURE_STRTOL
};
enum specdb_resource {
    SPECDB_RESOURCE_NONE, SPECDB_RESOURCE_HEAP, SPECDB_RESOURCE_FD,
    SPECDB_RESOURCE_FILE, SPECDB_RESOURCE_PIPE, SPECDB_RESOURCE_DIR,
    SPECDB_RESOURCE_MAPPING, SPECDB_RESOURCE_DL
};
struct specdb_contract {
    enum specdb_failure failure;
    enum specdb_resource acquires;
    enum specdb_resource releases;
    int returns_void;
    int format_index;              /* zero based; -1 if unknown/not a format API */
    int sets_errno;
    int dangerous;
    int truncation_size_index;     /* snprintf-like required-length result; -1 otherwise */
    char prototype[4096];
    char evidence[512];
    char source[128];
};

/* Extract only the requested declaration, including wrapped prototypes. */
int specdb_extract_prototype(const char *synopsis, const char *name,
                             char *out, size_t capacity);
int specdb_lookup_contract(sqlite3 *db, const char *name, struct specdb_contract *out);
/* Reviewed C/POSIX baseline, available independently of installed manpages. */
int specdb_builtin_contract(const char *name, struct specdb_contract *out);
/* Read-only loading never creates an empty database on a mistyped path. */
int specdb_open_readonly(const char *path, sqlite3 **out);

#ifdef __cplusplus
}
#endif

#endif /* SPECDB_H */

# specdb

Standalone tool and library for building a SQLite database of function
specifications extracted from Unix manpages.

Used by the [mancheck](../README.md) analyzer to extend its rule coverage
beyond the built-in static table, but can also be used independently for
any project that needs structured access to manpage data.

## Building

```sh
make
```

Produces:

- `specdb-build` -- CLI tool for populating the database
- `libspecdb.a` -- C library for querying the database

## Usage

### Index specific functions

```sh
./specdb-build spec.db 2 read write open close mmap
./specdb-build spec.db 3 malloc fopen printf
```

Arguments: `<db_path> <man_section> <func1> [func2 ...]`

### Scan an entire man section

```sh
./specdb-build spec.db --scan-section 2
./specdb-build spec.db --scan-section 3
```

Indexes everything reported by `man -k . <section>`.

### Scan all sections

```sh
./specdb-build spec.db --scan-all
```

The database is additive -- running specdb-build multiple times merges new
entries without removing existing ones.

## Database schema

### functions

| Column | Description |
|---|---|
| name | Function name (e.g. `read`) |
| section | Man section (e.g. `2`) |
| short_name | Left side of NAME line |
| short_desc | Right side of NAME line |
| header | Header file from SYNOPSIS (e.g. `<unistd.h>`) |
| proto | First prototype from SYNOPSIS |
| man_source | Source command (e.g. `man 2 read`) |
| raw | Full manpage output as plain text |

### function_sections

Per-function section content (DESCRIPTION, RETURN VALUE, ERRORS, etc.)
with original ordering preserved.

### function_aliases

Maps alternate names to their canonical function entry (e.g. `pread` -> `read`
when documented on the same manpage).

## C API

```c
#include "specdb.h"

sqlite3 *db;
specdb_open("spec.db", &db);

// Full function lookup
struct specdb_func func;
if (specdb_lookup_function(db, "read", "2", &func) == 0) {
    printf("%s: %s\n", func.name, func.short_desc);
    for (int i = 0; i < func.n_sections; i++)
        printf("  [%s]\n", func.sections[i].name);
    specdb_free_function(&func);
}

// Quick check: does this function have a RETURN VALUE section?
if (specdb_function_has_retval(db, "mmap") == 1)
    printf("mmap has documented return value\n");

specdb_close(db);
```

## Layout

```
specdb/
  src/
    specdb.c       # library (schema + queries)
    specdb.h       # public API header
    man2db.c       # specdb-build CLI
  include/
    specdb.h       # installed header (copy of src/specdb.h)
  data/
    spec.db        # pre-built manpage database
  Makefile
```

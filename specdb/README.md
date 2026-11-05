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

// A heading alone is not a failure contract.
struct specdb_contract contract;
if (specdb_lookup_contract(db, "mmap", &contract) == 1) {
    printf("%s\n", contract.prototype);
    printf("evidence: %s\n", contract.evidence);
}

// Reviewed semantics are available even without a manpage database.
specdb_builtin_contract("mmap", &contract);
// contract.failure == SPECDB_FAILURE_MAP_FAILED

specdb_close(db);
```

## Layout

```
specdb/
  src/
    specdb.c       # library (schema + queries)
    contracts.c    # signature/prose extraction and reviewed API contracts
    specdb.h       # public API header
    man2db.c       # specdb-build CLI
  include/
    specdb.h       # development forwarding header; install copies src/specdb.h
  data/
    spec.db        # pre-built manpage database
  Makefile
```

The analyzer uses `specdb_open_readonly`, which rejects missing, empty and
incompatible databases without creating files. `specdb_open` remains the
schema-creating API for builders. Page refreshes are transactional. The builder
runs `man` with an argv array and rejects failed or malformed command output.

Contract extraction is conservative: a complete matching declaration is needed;
void functions are infallible; only attributed failure prose is interpreted.
Shared-page references and historical descriptions are not general API rules.
Unknown contracts do not acquire a must-check rule merely from a section heading.

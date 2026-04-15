# specdb – Manpage-based specification database for Mancheck

`specdb` is the component of **Mancheck** that builds and maintains a
SQLite database of function specifications extracted from system
manual pages.

The **analyzer** component never calls `man(1)` directly. Instead, it
consumes the database built by `specdb`.

## Features

- Uses `man SECTION NAME | col -b` as the canonical source.
- Parses all sections of the manpage (NAME, SYNOPSIS, DESCRIPTION,
  RETURN VALUE, ERRORS, NOTES, etc.) using a generic heuristic.
- Stores:
  - High-level metadata per function (`functions` table):
    - `name`, `section`
    - `short_name`, `short_desc` (from `NAME`)
    - `header`, `proto` (best-effort from `SYNOPSIS`)
    - `man_source` (e.g. `"man 2 read"`)
    - `raw` (full man output as plain text)
  - Full section content (`function_sections` table):
    - `section_name` (e.g. `"DESCRIPTION"`, `"RETURN VALUE"`)
    - `content` (full text of that section)
    - `ord` (original section order)

- Provides a small C API for consumers:
  - `specdb_open` / `specdb_close`
  - `specdb_lookup_function`
  - `specdb_free_function`

You can use this directly from `analyzer/` or from other tools.

## Layout

```text
specdb/
  include/
    specdb.h       # public API for consumers
  src/
    specdb.c       # schema + lookup functions
    man2db.c       # specdb-build CLI (man → DB)
  data/
    (empty, DB files live here at runtime)
  Makefile

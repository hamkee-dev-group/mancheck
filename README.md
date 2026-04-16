# mancheck

A static analysis tool for C that checks code against the documented contracts
of standard library and system calls, as described in Unix manpages.

```
example.c:4:5: ignored return of read(): unchecked return value of read()
example.c:9:5: warning: use of dangerous function gets()
example.c:12:5: warning: non-literal format string in printf()
```

## What it checks

- **Unchecked return values** -- `read()`, `malloc()`, `fopen()`, `pthread_create()`, and ~150 other functions whose manpages say the return value signals success or failure.
- **Dangerous functions** -- `gets()`, `strcpy()`, `sprintf()`, `strtok()`, and others that are inherently unsafe.
- **Format string misuse** -- non-literal format arguments to `printf()`, `scanf()`, `snprintf()`, etc.
- **malloc size mismatch** -- `malloc(sizeof(p))` where `sizeof(*p)` was likely intended.
- **Double close** -- calling `close()` or `fclose()` twice on the same descriptor/handle.

When connected to **specdb** (a database of parsed manpages), the analyzer
automatically extends its coverage to any function that has a documented
RETURN VALUE section -- over 2500 additional functions without maintaining
a hand-written list.

## Project structure

```
mancheck/
  analyzer/       C analyzer (tree-sitter based)
  specdb/         Manpage contract database + builder
  mc_tests/       Integration and golden tests (73 assertions)
  vendor/         Vendored tree-sitter runtime and C grammar
  Makefile        Top-level build
```

## Requirements

- Linux or macOS (POSIX)
- C11 compiler (gcc or clang)
- make
- libsqlite3 development headers (`libsqlite3-dev` / `sqlite3-dev`)
- sqlite3 CLI (for tests)
- Manpages installed (for populating specdb)

Tree-sitter and the C grammar are vendored as git submodules. No other
external dependencies.

## Building

```sh
git clone --recurse-submodules <repo-url>
cd mancheck
make            # builds specdb (library + specdb-build) then the analyzer
make test       # builds then runs the full test suite
make clean      # cleans all build artifacts
```

This produces two binaries:

- `analyzer/analyzer` -- the static analyzer
- `specdb/specdb-build` -- standalone tool for building/updating the manpage database

## Quick start

Analyze a file using the built-in rule table (no setup required):

```sh
./analyzer/analyzer --no-db example.c
```

For broader coverage, build a specdb from your system's manpages:

```sh
./specdb/specdb-build specdb/data/spec.db --scan-section 2
./specdb/specdb-build specdb/data/spec.db --scan-section 3
```

Then analyze with `--specdb`:

```sh
./analyzer/analyzer --no-db --specdb specdb/data/spec.db example.c
```

## Usage

```
analyzer [options] <file.c> [file.c ...]
```

| Option | Description |
|---|---|
| `--specdb PATH` | Load manpage database for extended rule coverage |
| `--db PATH` | Write run results to a SQLite database (default: `mancheck.db`) |
| `--no-db` | Disable the run database entirely |
| `--json` | Output results as JSON instead of text |
| `--dump-views PATH` | Dump internal preprocessing views as JSONL (for debugging) |

### Examples

```sh
# Basic analysis, text output
./analyzer/analyzer --no-db src/*.c

# With specdb + run database
./analyzer/analyzer --specdb specdb/data/spec.db --db results.db src/*.c

# JSON output for tooling
./analyzer/analyzer --json --no-db --specdb specdb/data/spec.db src/*.c
```

## How rules work

The analyzer uses two rule sources:

1. **Static table** (~150 functions) -- compiled into the analyzer with hand-curated flags: `RETVAL_MUST_CHECK`, `DANGEROUS`, `FORMAT_STRING`. This covers the most common POSIX and C standard library functions.

2. **specdb** (optional, ~2500 functions) -- when `--specdb` is passed, any function call not found in the static table is looked up in the manpage database. If the function has a documented RETURN VALUE section, it gets `RETVAL_MUST_CHECK` automatically.

The static table always takes priority. specdb only fills in gaps.

## Building the manpage database

The database is not checked in (it's ~50MB and system-specific). Build it
from your local manpages:

```sh
# Index specific functions
./specdb/specdb-build specdb/data/spec.db 2 mmap munmap msync

# Index an entire man section
./specdb/specdb-build specdb/data/spec.db --scan-section 2
./specdb/specdb-build specdb/data/spec.db --scan-section 3

# Index everything (large)
./specdb/specdb-build specdb/data/spec.db --scan-all
```

The database is additive -- running specdb-build multiple times merges new
entries without removing existing ones.

## Run database

When `--db` is used (or by default with `mancheck.db`), the analyzer records
each run in a SQLite database:

- **runs** table -- one row per analyzed file, with timestamp and error count
- **facts** table -- individual findings (unchecked return, warning, etc.)
- **facts_fts** -- full-text search index over facts

This enables querying results after the fact:

```sh
sqlite3 results.db "SELECT * FROM facts WHERE symbol = 'read';"
sqlite3 results.db "SELECT * FROM facts_fts WHERE facts_fts MATCH 'malloc';"
```

## Tests

```sh
make test
```

The test suite (73 assertions) covers:

- Golden output tests for 36 C test files
- Database run tracking and fact recording
- specdb core queries and coverage
- JSON output validation
- Multi-file analysis
- FTS search
- specdb-analyzer integration (with/without `--specdb`)

Golden test expectations live in `mc_tests/tests/*.exp`. To rebaseline after
intentional output changes:

```sh
bash mc_tests/rebaseline_golden.sh
```

## Architecture

```
C source file
    |
    v
[preprocessor pipeline]
    mc_load_file -> mc_preprocess_minimal -> clang -E -> user-line extraction
    |
    v
[tree-sitter parser]
    TSQuery matches call_expression nodes
    |
    v
[call classification]
    unchecked / checked_cond / stored / propagated / ignored_explicit
    |
    v
[rule lookup]
    static table -> specdb fallback (if loaded)
    |
    v
[reporters]
    text output / JSON output / SQLite facts
```

### Call classification

For each function call found in the AST, the analyzer classifies how the
return value is used:

| Classification | Meaning |
|---|---|
| `unchecked` | Return value discarded (expression statement) |
| `checked_cond` | Used in an if/while/for condition |
| `stored` | Assigned to a variable or passed as argument |
| `propagated` | Returned from the enclosing function |
| `ignored_explicit` | Cast to `(void)` -- intentional discard |

Only `unchecked` and `ignored_explicit` trigger warnings for `RETVAL_MUST_CHECK`
functions.

## Design

- **Manpage-driven**: contracts come from documentation, not heuristics
- **Two-database model**: specdb (static, reusable) and run DB (per-analysis)
- **Tree-sitter parsing**: fast, incremental, no build system integration needed
- **Static table + specdb fallback**: works out of the box, improves with data
- **SQLite everywhere**: inspectable, queryable, portable

## Showcase: what mancheck catches that others miss

The `mc_tests/tests/showcase_*.c` files demonstrate bug classes where mancheck
(current and planned) adds value over cppcheck and clang-tidy/clang-analyzer.
Run the comparison yourself:

```sh
bash mc_tests/run_showcase_comparison.sh
```

### Bug classes and tool coverage

| Bug class | cppcheck | clang-tidy | mancheck (current) | mancheck (planned) |
|---|---|---|---|---|
| **Wrong error-checking protocol** (strtol==-1, open==0, pthread+errno) | -- | -- | -- | specdb RETURN VALUE |
| **errno misuse** (stale errno, wrong errno protocol) | -- | partial (unix.Errno) | -- | specdb ERRORS section |
| **Partial write/read not looped** (write, send, fread) | -- | -- | -- | specdb RETURN VALUE |
| **snprintf truncation ignored** (return >= size) | -- | cert-err33-c (unchecked only) | unchecked return | specdb truncation |
| **Resource cleanup mismatch** (opendir+fclose, fopen+close, popen+fclose) | -- | -- | -- | specdb SYNOPSIS pairing |
| **Linux-only APIs** (pipe2, epoll, unshare) | -- | -- | -- | specdb 3 vs 3posix diff |
| **POSIX obsolescent** (usleep, asctime, ftime, bcopy) | partial (gets, asctime) | partial (bcopy, bzero, gets) | partial (gets, tmpnam, sprintf) | specdb 3posix status |
| **Missing headers** (read without unistd.h) | -- | implicit-function-declaration | -- | specdb SYNOPSIS headers |
| **Async-signal-unsafe** (printf in signal handler) | -- | bugprone-signal-handler | -- | specdb signal-safety(7) |
| **MT-Unsafe in threads** (strtok, localtime, getpwnam) | -- | concurrency-mt-unsafe | partial (strtok, getenv) | specdb ATTRIBUTES |

### Key findings from the comparison

**Nobody catches** (mancheck's opportunity):
- Wrong error protocol: `strtol()` checked with `== -1`, `open()` checked with `== 0`, `pthread_create()` errors checked via errno
- Partial write bugs: single `write()` call without loop on pipes/sockets
- snprintf truncation: return value stored but not compared to buffer size
- Resource cleanup mismatch: `opendir()` closed with `fclose()`, `popen()` closed with `fclose()`
- Linux-only APIs used without portability guards: `pipe2()`, `epoll_create1()`, `unshare()`

**clang-tidy catches but cppcheck misses**: signal safety, thread safety, bcopy/bzero

**cppcheck catches but clang-tidy misses**: asctime deprecation, resource leaks

**mancheck already catches uniquely**: broader unchecked return coverage via specdb (2500+ functions vs clang-tidy's ~150 in cert-err33-c)

### Showcase files

| File | Bug class |
|---|---|
| `showcase_wrong_error_check.c` | Wrong error-checking protocol per man page |
| `showcase_errno_misuse.c` | errno protocol violations |
| `showcase_partial_write.c` | Short write/read not handled |
| `showcase_snprintf_truncation.c` | snprintf truncation undetected |
| `showcase_resource_cleanup.c` | Allocation/deallocation pairing mismatch |
| `showcase_portability.c` | Linux-only APIs, POSIX obsolescent functions |
| `showcase_deprecated_posix.c` | POSIX-deprecated and removed interfaces |
| `showcase_signal_safety.c` | Async-signal-unsafe calls in signal handlers |
| `showcase_thread_safety.c` | MT-Unsafe functions in threaded code |
| `showcase_header_mismatch.c` | Missing required headers per man page |

## Limitations

- No control flow analysis -- doesn't track values across branches
- No interprocedural analysis -- doesn't follow wrapper functions
- No macro expansion awareness -- operates on preprocessed source
- Dangerous/format-string flags are static-table only (not inferred from manpages)

## License

See individual source files for licensing information.

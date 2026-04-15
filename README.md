# mancheck

**mancheck** is a static analysis tool for C that checks code against the
**documented contracts of standard library and system calls**, as described
in Unix manpages.

Instead of heuristics, mancheck answers questions like:

- Is the return value of `read()` checked?
- Is `chmod()` checked correctly according to its manpage?
- Is an error-signaling convention being misused?

The tool is **manpage-driven** and **contract-based**.

---

## Overview

mancheck consists of three components:

```
mancheck/
├── analyzer/      # C analyzer (tree-sitter based)
├── specdb/        # Manpage-derived contract database
├── mc_tests/      # Integration & golden tests
```

### High-level pipeline

```
manpages ──▶ specdb-build ──▶ spec.db
                                │
                                ▼
                          analyzer + C code
                                │
                                ▼
                     diagnostics / JSON / SQLite
```

There are **two databases by design**:

- **specdb** (static): function contracts extracted from manpages
- **run database** (dynamic): results of a single analyzer run

---

## Requirements

- POSIX system (Linux, OpenBSD, macOS)
- C compiler
- `make`
- `sqlite3`
- Manpages installed (`man`, `man -k`)

Tree-sitter and the C grammar are vendored — no external runtime dependencies.

---

## Building

### Build the analyzer

```sh
cd analyzer
make
```

This produces:

```
analyzer/analyzer
```

### Build the spec database tool

```sh
cd specdb
make
```

This produces:

```
specdb/specdb-build
```

---

## Step 1: Build a spec database

The analyzer **requires a spec database**.  
It will not do anything useful without one.

### Minimal example (recommended)

Build a database for a small, explicit set of functions:

```sh
./specdb/specdb-build spec.db 2 read write open close chmod
```

Arguments:
- `spec.db` → output database
- `2` → man section (system calls)
- remaining arguments → function names

This is the **best way to start**: small, predictable, and debuggable.

---

### Scan a whole man section (advanced)

```sh
./specdb/specdb-build spec.db --scan-section 2
```

This indexes everything reported by:

```
man -k . 2
```

⚠️ This creates a large database and may introduce noisy rules.

---

### Scan all sections (not recommended initially)

```sh
./specdb/specdb-build spec.db --scan-all
```

This is primarily for experimentation.

---

## Step 2: Run the analyzer

### Basic usage

```sh
./analyzer/analyzer --db spec.db example.c
```

This:
- parses `example.c` using tree-sitter
- applies rules from `spec.db`
- prints diagnostics to stdout
- records results in a per-run SQLite database

---

### Analyze multiple files

```sh
./analyzer/analyzer --db spec.db src/*.c
```

---

### JSON output

```sh
./analyzer/analyzer --json --db spec.db example.c
```

Useful for tooling or editor integration.

---

### Dump internal AST “views” (developer/debug)

```sh
./analyzer/analyzer --db spec.db example.c --dump-views views.jsonl
```

This emits internal tree-sitter views as JSONL.  
This is **not user-facing**, but useful for rule development.

---

## Output model

mancheck distinguishes between **facts** and **errors**:

- **Facts**: evidence that a rule matched (e.g. a call to `read()`)
- **Errors**: violations of a documented contract

Internally, each run is recorded in a SQLite database with tables such as:
- `runs`
- `facts`
- `errors`

Tests and tooling query this database directly.

---

## Example

```c
#include <unistd.h>

void f(int fd, char *buf) {
    read(fd, buf, 10);   /* unchecked */
}
```

Output:

```
example.c:4: unchecked return value from read()
```

---

## Tests

Integration and golden tests live in:

```
mc_tests/
```

Run all tests:

```sh
cd mc_tests
./run.sh
```

This will:
- build the analyzer if needed
- run analysis on test files
- query SQLite results directly
- compare output against `.exp` files

If all tests pass, the system is working end-to-end.

---

## Design goals (current)

- Manpage-driven contracts
- No heuristics
- No CFG or whole-program analysis (yet)
- Correctness over completeness
- Explicit, inspectable data model (SQLite)

---

## Non-goals (for now)

- Full semantic analysis
- Alias analysis
- Macro expansion
- Wrapper function inference

These may be explored later, but are intentionally excluded.

---

## Status

The core pipeline is **working and tested**.

What is intentionally minimal today:
- UX polish
- documentation beyond this README
- breadth of manpage coverage

---

## Philosophy

> If the manual says “you must check this”, the code should check it.

mancheck exists to enforce **documented contracts**, not inferred intent.

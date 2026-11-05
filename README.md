# mancheck

A C analyzer that combines reviewed API contracts, Unix manpage signatures and
return-value documentation with intraprocedural value and resource tracking.
Its useful distinction is checking *how* a result is tested, as well as whether
it is used. It complements compiler warnings, static analyzers and sanitizers.

```c
int rc = pthread_mutex_lock(&lock);
if (rc < 0) { /* pthread errors are positive error numbers */
    return;
}
```

Mancheck reports `wrong_error_check` for this predicate. It also checks `mmap()`
against `MAP_FAILED` and identifies comparisons treating `-1` as a `strtol()`
failure sentinel. These are focused checks, not a complete implementation of
all the protocols described in manpages.

## Build and run

Requirements: a C11 compiler, Clang for preprocessing, make, SQLite development
headers, and the vendored tree-sitter runtime/C grammar. Tests also use Python 3
and the sqlite3 CLI. Importing documentation requires `man` and installed pages.

```sh
make
./analyzer/analyzer --no-db example.c
./analyzer/analyzer --json --no-db example.c
./analyzer/analyzer --gcc --no-db example.c
./analyzer/analyzer --sarif --no-db example.c
```

For project build flags:

```sh
./analyzer/analyzer --no-db --compile-commands build/compile_commands.json src/file.c
```

The `arguments` array takes precedence over `command`. Arguments are passed
directly to Clang; command strings are tokenized without running a shell.
Supported preprocessing flags include language standard, `-D`/`-U`, include
search paths, forced includes, sysroots, targets and `-pthread`. The fallback
without a matching entry uses Clang with `-std=c11`; it does not reproduce every
compiler's built-in definitions or every driver option. Response files and
`-Wp,`/`-Xpreprocessor` forwarding fail explicitly because they are not modeled.

## Checks

| Category | Behavior |
| --- | --- |
| `return_value_check` | Detects discarded results and stored results that reach an exit without validation or propagation. Tracks assignments, aliases, lexical scope and branches. |
| `wrong_error_check` | Checks selected predicates against reviewed pthread, mmap and integer-conversion failure contracts. |
| `missing_cleanup` | Reports a locally acquired resource that can reach an exit without release or ownership transfer. |
| `resource_mismatch` | Detects incompatible acquisition/release families, such as `popen()` followed by `fclose()`. |
| `double_close` | Detects a definitely released value being released again; includes `free()`, descriptors and other reviewed resource families. |
| `format_string` | Uses the documented format-parameter index; accepts parenthesized and concatenated string literals. |
| `malloc_size_mismatch` | Detects pointer allocation using `sizeof(pointer)` across whitespace variants. |
| `dangerous_function` | Retains the existing API audit policy for functions such as `gets`, `strcpy`, and `strtok`. A policy warning is not proof that a particular call is unsafe. |

Environment access is an optional audit policy, disabled by default:

```sh
MANCHECK_AUDIT_ENV=1 ./analyzer/analyzer --no-db example.c
```

## Manpage contracts

```sh
./specdb/specdb-build specdb/data/spec.db --scan-section 2
./specdb/specdb-build specdb/data/spec.db --scan-section 3
./analyzer/analyzer --no-db --specdb specdb/data/spec.db example.c
```

Reviewed built-in contracts are authoritative for their named APIs. For other
APIs, specdb lookup extracts that function's complete declaration, identifies
void returns and variadic format parameters, and conservatively interprets
attributed RETURN VALUE prose. Unknown behavior remains unknown. A RETURN VALUE
heading alone does **not** imply a fallible function; words such as “obsolete”
elsewhere on a shared page do **not** mark every function dangerous.

Extraction is still heuristic. It does not understand arbitrary natural-language
contracts. The public `specdb_contract` structure includes the prototype,
failure convention, resource family, format position, evidence and source.
Existing databases work without migration; reimport pages to repair previously
stored `proto` metadata. The analyzer opens specdb read-only and rejects missing,
empty or incompatible databases. The builder executes `man` directly, checks
its status and refreshes each page transactionally.

See [specdb/README.md](specdb/README.md) for the C API.

## Output and suppression

| Option | Meaning |
| --- | --- |
| `--json` | JSON call inventory and complete diagnostic array |
| `--gcc` | GCC-style warnings on stderr |
| `--sarif` | SARIF results |
| `--warn-exit` | Nonzero status when findings remain after suppression |
| `--db PATH` / `--no-db` | Persist diagnostic facts / disable persistence |
| `--specdb PATH` | Read additional API documentation |
| `--compile-commands PATH` / `--compdb PATH` | Per-file preprocessing configuration |
| `--dump-views PATH` | Write raw, comment-masked and preprocessed views as JSONL |
| `--suppressions PATH` | Load external suppression rules; otherwise `.mancheckrc` in the working directory is used if present |

Text, JSON and SARIF apply suppression in source order and persist the same
accepted diagnostics. JSON `calls` is an inventory, including checked calls;
`diagnostics` is the authoritative list of findings. `issue_count` counts those
findings once. JSON `complete` indicates successful execution of the implemented
analysis, not a proof of correctness or exhaustive C coverage.

Unreadable inputs, failed preprocessing, parse failures, analysis limits,
contract lookup failures and persistence failures produce nonzero exit status.
Failed preprocessing can still produce a partial raw-source report, marked
incomplete. Ordinary findings return zero in text/JSON mode unless `--warn-exit`
is set; GCC and SARIF modes return nonzero for findings by default.

The `--db` results database records a schema version (`PRAGMA user_version`).
Older or partially created mancheck databases are completed and upgraded in one
transaction; databases from other tools or newer versions are refused without
being modified. Each run row has a `status` of `started`, `completed` or
`failed`; `failed` means the analysis or the persistence of its findings did
not finish.

Suppressions use the existing `path-glob category` format. Inline markers such
as `// mc:ignore-next-warning` consume one finding; a marker applies to the first
eligible finding in source order, regardless of which pass discovered it.

## Verification and comparison

```sh
make test                         # deterministic regressions + legacy integration/golden tests
make test-sanitize                # ASan, UBSan and leak checks on deterministic regressions
python3 mc_tests/compare_contracts.py
```

The comparison compiles each fixture first, records tool commands and versions,
and tests four suspicious predicates and two correct controls. Results are
written to `mc_tests/out/contract_comparison.json`. In the recorded local run,
mancheck diagnosed all four predicates, while Clang Static Analyzer, cppcheck
and GCC `-fanalyzer` were quiet; all tools were quiet on the two controls.
This is evidence for these examples and configurations, not an exclusivity claim
about every version, checker, custom model or sanitizer workload. ASan/UBSan are
used to validate mancheck itself; they do not establish application-level intent.

The legacy environment-audit tests explicitly enable `MANCHECK_AUDIT_ENV=1`.
New regressions use synthetic manpages and valid C positive/negative cases and
therefore do not depend on the host's installed documentation.

## Scope and limits

- The value analysis is intraprocedural. It builds control-flow edges for
  branches, loops, returns, ordinary gotos and switch case alternatives. Joins
  forget ambiguous aliases. It does not prove arbitrary numerical predicates,
  loop iteration counts or correlations between separate conditions.
- Switch case values are not constrained; cleanup diagnostics are disabled for
  functions containing switches. Short-circuit and conditional expressions use
  conservative joins. There is no full heap, field, function-pointer or
  interprocedural ownership model.
- Resources passed to unknown callees conservatively escape. Conditional
  ownership transfers such as `realloc` and `fdopen` are approximated. This can
  miss leaks; release operations are treated as consuming their resource even
  when a particular platform can retain it after failure.
- A comparison can be intentional even when it is not a valid failure test.
  These diagnostics require review. General errno/end-pointer sequencing,
  snprintf truncation, partial I/O loops, signal safety, lock protocols and
  portability reasoning are not implemented.
- Preprocessing analyzes the main source file and the project headers it
  includes; system headers (as classified by the compiler, e.g. `-isystem` and
  the default system paths) are skipped. Header findings carry the header's
  path, and a header included by several sources is reported for each of them.
  Inline suppression markers apply only to the file that contains them; use a
  suppression file for header findings. Macros can expand calls; columns
  reflect expanded text. Unusual `#line` mappings may have approximate locations.
- Allocation failures and explicit work/depth/memory limits are reported as
  failures. “No findings” is never a proof that C code is safe.

## Source layout

`analyzer/src/mc_flow.c` implements control-flow/value/resource analysis;
`mc_ts.c` handles syntax checks and reporting; `mc_rules.c` combines the reviewed
policy and contract cache. `specdb/src/contracts.c` owns contract extraction and
reviewed API semantics.

## License

See individual source files for licensing information.

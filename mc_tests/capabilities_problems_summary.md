# mancheck capabilities / problems summary

Manually maintained. Sources: `README.md`, `mc_tests/run.sh`, `mc_tests/tests/test01_simple_unchecked.c`, `mc_tests/tests/test16_dangerous_functions.c`, `mc_tests/tests/test17_format_string.c`, `mc_tests/tests/double_close.c`, `mc_tests/tests/env_usage.c`, `mc_tests/tests/malloc_bad.c`.

## Capabilities

- Flags unchecked returns such as `read()` and `write()` (`mc_tests/tests/test01_simple_unchecked.c`); `README.md` says the built-in table also covers `malloc()`, `fopen()`, `pthread_create()`, and about 150 similar calls.
- Warns on dangerous functions such as `gets()`, `strcpy()`, `sprintf()`, and `strtok()` (`README.md`, `mc_tests/tests/test16_dangerous_functions.c`).
- Warns on non-literal format strings in `printf()`-family calls (`README.md`, `mc_tests/tests/test17_format_string.c`).
- `mc_tests/run.sh` also asserts `insecure_env_usage` (`mc_tests/tests/env_usage.c`), `malloc_size_mismatch` (`mc_tests/tests/malloc_bad.c`), `double_close` (`mc_tests/tests/double_close.c`), plus JSON, SARIF, SQLite facts, and suppressions.

## Problems / limitations

- Broader return-value coverage is optional: `README.md` says the built-in table is about 150 functions, and wider coverage needs `--specdb`.
- `specdb` is not checked in and is system-specific; `README.md` says it must be built from local manpages.
- `README.md` says only `unchecked` and `ignored_explicit` uses warn for return-value rules; `checked_cond`, `stored`, and `propagated` are classified but do not produce return-value warnings.
- This is a short category summary, not a complete rules list; exact behavior is defined by `mc_tests/run.sh` and the fixtures under `mc_tests/tests/`.

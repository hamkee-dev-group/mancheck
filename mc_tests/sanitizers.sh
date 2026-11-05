#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
work="$(mktemp -d "${TMPDIR:-/tmp}/mancheck-sanitizers.XXXXXX")"
trap 'rm -rf "$work"' EXIT
cd "$root"
cc="${SANITIZER_CC:-clang}"
flags=(-std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer)
"$cc" "${flags[@]}" -Ianalyzer/include -Ianalyzer/src -Ianalyzer/libdb -Ispecdb/src \
    -Ivendor/tree-sitter/lib/include -Ivendor/tree-sitter/lib/src -Ivendor/tree-sitter-c/src \
    -include analyzer/include/mc_ts_compat.h \
    analyzer/src/main.c analyzer/src/mc_ts.c analyzer/src/mc_flow.c analyzer/src/mc_rules.c \
    analyzer/src/mc_db_integration.c analyzer/src/report.c analyzer/src/mc_suppress.c \
    analyzer/src/mc_compdb.c analyzer/src/mc_preproc.c analyzer/libdb/mc_db.c \
    specdb/src/specdb.c specdb/src/contracts.c \
    vendor/tree-sitter/lib/src/lib.c vendor/tree-sitter-c/src/parser.c \
    -lsqlite3 -o "$work/analyzer"
"$cc" "${flags[@]}" specdb/src/specdb.c specdb/src/contracts.c specdb/src/man2db.c \
    -lsqlite3 -o "$work/specdb-build"
export ASAN_OPTIONS=detect_leaks=1:halt_on_error=1
export UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1
export MANCHECK_ANALYZER="$work/analyzer"
export MANCHECK_BUILDER="$work/specdb-build"
python3 mc_tests/regressions.py

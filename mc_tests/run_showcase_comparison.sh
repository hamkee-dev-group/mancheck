#!/usr/bin/env bash
#
# Compare mancheck showcase tests against cppcheck and clang-tidy/clang-analyzer.
# Shows which bugs each tool catches and which it misses.
#
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
TESTS_DIR="${ROOT_DIR}/mc_tests/tests"
ANALYZER_BIN="${ROOT_DIR}/analyzer/analyzer"
SPECDB_PATH="${ROOT_DIR}/specdb/data/spec.db"

COLOR_RED="$(printf '\033[31m')"
COLOR_GREEN="$(printf '\033[32m')"
COLOR_YELLOW="$(printf '\033[33m')"
COLOR_CYAN="$(printf '\033[36m')"
COLOR_BOLD="$(printf '\033[1m')"
COLOR_RESET="$(printf '\033[0m')"

separator() {
    printf '%s%s%s\n' "$COLOR_CYAN" "$(printf '%.0s─' {1..72})" "$COLOR_RESET"
}

header() {
    printf '\n%s%s %s %s\n' "$COLOR_BOLD" "$COLOR_YELLOW" "$1" "$COLOR_RESET"
    separator
}

tool_header() {
    printf '  %s%s▶ %s%s\n' "$COLOR_BOLD" "$COLOR_CYAN" "$1" "$COLOR_RESET"
}

show_findings() {
    local output="$1"
    if [ -z "$output" ] || [ "$output" = "" ]; then
        printf '    %s(no findings)%s\n' "$COLOR_GREEN" "$COLOR_RESET"
    else
        echo "$output" | sed 's/^/    /'
    fi
}

# Build analyzer if needed
if [ ! -x "$ANALYZER_BIN" ]; then
    echo "Building analyzer..."
    make -C "${ROOT_DIR}/analyzer" -j"$(nproc)" >/dev/null 2>&1
fi

SHOWCASE_FILES=(
    "showcase_wrong_error_check.c"
    "showcase_signal_safety.c"
    "showcase_thread_safety.c"
    "showcase_portability.c"
    "showcase_header_mismatch.c"
    "showcase_errno_misuse.c"
    "showcase_partial_write.c"
    "showcase_snprintf_truncation.c"
    "showcase_resource_cleanup.c"
    "showcase_deprecated_posix.c"
)

for testfile in "${SHOWCASE_FILES[@]}"; do
    src="${TESTS_DIR}/${testfile}"
    [ -f "$src" ] || { echo "SKIP: $src not found"; continue; }

    header "$testfile"

    # --- cppcheck ---
    tool_header "cppcheck (--enable=all --std=c11)"
    cpp_out="$(cppcheck --enable=all --std=c11 \
        --suppress=missingIncludeSystem \
        --suppress=missingInclude \
        --check-level=exhaustive \
        "$src" 2>&1 | grep -vE '^\^$|^Checking |^$|nofile:0' || true)"
    show_findings "$cpp_out"
    echo

    # --- clang-tidy ---
    tool_header "clang-tidy (bugprone-*, cert-*, concurrency-*)"
    ct_out="$(clang-tidy \
        -checks='-*,bugprone-*,cert-*,concurrency-*,clang-analyzer-*' \
        "$src" -- -std=c11 -pthread 2>/dev/null \
        | grep -E 'warning:|error:' || true)"
    show_findings "$ct_out"
    echo

    # --- clang -Wall -Wextra -Weverything (compile warnings) ---
    tool_header "clang -Weverything -fsyntax-only"
    clang_out="$(clang -Weverything -Wno-declaration-after-statement \
        -Wno-disabled-macro-expansion \
        -Wno-padded -Wno-reserved-identifier \
        -Wno-unsafe-buffer-usage \
        -Wno-pre-c11-compat \
        -Wno-missing-noreturn \
        -fsyntax-only -std=c11 -pthread "$src" 2>&1 \
        | grep -E 'warning:|error:' || true)"
    show_findings "$clang_out"
    echo

    # --- mancheck ---
    tool_header "mancheck (current)"
    mc_out="$(cd "$ROOT_DIR" && "$ANALYZER_BIN" --no-db "$src" 2>&1 || true)"
    show_findings "$mc_out"

    # --- mancheck with specdb ---
    if [ -f "$SPECDB_PATH" ]; then
        tool_header "mancheck --specdb (extended)"
        mc_spec_out="$(cd "$ROOT_DIR" && "$ANALYZER_BIN" --no-db --specdb "$SPECDB_PATH" "$src" 2>&1 || true)"
        show_findings "$mc_spec_out"
    fi

    echo
done

separator
printf '%s%sDone. Review above to see which bugs each tool catches vs misses.%s\n' \
    "$COLOR_BOLD" "$COLOR_GREEN" "$COLOR_RESET"

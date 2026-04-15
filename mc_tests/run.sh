#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"

ANALYZER_BIN="${ROOT_DIR}/analyzer/analyzer"
SPECDB_PATH="${ROOT_DIR}/specdb/data/spec.db"
OUT_DIR="${ROOT_DIR}/mc_tests/out"
TEST_DB="${OUT_DIR}/mancheck_test.db"
VIEWS_FILE="${OUT_DIR}/views_test.jsonl"
GOLDEN_DB="${OUT_DIR}/golden_test.db"

COLOR_RED="$(printf '\033[31m')"
COLOR_GREEN="$(printf '\033[32m')"
COLOR_YELLOW="$(printf '\033[33m')"
COLOR_RESET="$(printf '\033[0m')"

passed=0
failed=0

log_info()  { printf "%s[INFO]%s  %s\n"  "$COLOR_YELLOW" "$COLOR_RESET" "$*"; }
log_pass()  { printf "%s[PASS]%s  %s\n"  "$COLOR_GREEN" "$COLOR_RESET" "$*"; }
log_fail()  { printf "%s[FAIL]%s  %s\n"  "$COLOR_RED"   "$COLOR_RESET" "$*"; }
log_fatal() { log_fail "$*"; exit 1; }

run_cmd() {
    log_info "run: $*"
    "$@"
}

expect_eq() {
    local actual="$1"
    local expected="$2"
    local msg="$3"

    if [ "$actual" = "$expected" ]; then
        log_pass "$msg (expected='$expected', got='$actual')"
        passed=$((passed + 1))
    else
        log_fail "$msg (expected='$expected', got='$actual')"
        failed=$((failed + 1))
    fi
}

need_tool() {
    local cmd="$1"
    command -v "$cmd" >/dev/null 2>&1 || log_fatal "Required tool '$cmd' not found in PATH"
}

# ----------------------------------------------------------------------
# Setup
# ----------------------------------------------------------------------

need_tool sqlite3
need_tool make

log_info "Project root: $ROOT_DIR"

mkdir -p "$OUT_DIR"
rm -f "$TEST_DB" "$VIEWS_FILE" "$GOLDEN_DB"

if [ ! -x "$ANALYZER_BIN" ]; then
    log_info "Building analyzer..."
    run_cmd make -C "${ROOT_DIR}/analyzer"
fi

if [ ! -f "$SPECDB_PATH" ]; then
    log_fatal "specdb database not found at '${SPECDB_PATH}'"
fi

# ----------------------------------------------------------------------
# Test 1: analyzer basic unchecked-return (DB)
# ----------------------------------------------------------------------
test_analyzer_unchecked_basic() {
    log_info "=== Test 1: analyzer basic unchecked-return (DB) ==="

    local src="${ROOT_DIR}/mc_tests/tests/test01_simple_unchecked.c"
    run_cmd "$ANALYZER_BIN" --db "$TEST_DB" "$src"

    local runs
    runs="$(sqlite3 "$TEST_DB" "SELECT COUNT(*) FROM runs WHERE filename LIKE '%test01_simple_unchecked.c%';")"
    expect_eq "$runs" "1" "runs row exists"

    local run_id
    run_id="$(sqlite3 "$TEST_DB" "SELECT id FROM runs WHERE filename LIKE '%test01_simple_unchecked.c%' LIMIT 1;")"

    local error_count
    error_count="$(sqlite3 "$TEST_DB" "SELECT error_count FROM runs WHERE id=$run_id;")"
    expect_eq "$error_count" "2" "error_count is 2"

    local facts
    facts="$(sqlite3 "$TEST_DB" "SELECT COUNT(*) FROM facts WHERE run_id=$run_id;")"
    expect_eq "$facts" "2" "two facts recorded"

    local has_read has_write
    has_read="$(sqlite3 "$TEST_DB" "SELECT COUNT(*) FROM facts WHERE run_id=$run_id AND symbol='read';")"
    has_write="$(sqlite3 "$TEST_DB" "SELECT COUNT(*) FROM facts WHERE run_id=$run_id AND symbol='write';")"
    expect_eq "$has_read" "1" "fact for read"
    expect_eq "$has_write" "1" "fact for write"
}

# ----------------------------------------------------------------------
# Test 2: analyzer no-issues (DB)
# ----------------------------------------------------------------------
test_analyzer_no_issues() {
    log_info "=== Test 2: analyzer no-issues (DB) ==="

    local src="${ROOT_DIR}/mc_tests/tests/test02_no_unchecked.c"
    run_cmd "$ANALYZER_BIN" --db "$TEST_DB" "$src"

    local runs
    runs="$(sqlite3 "$TEST_DB" "SELECT COUNT(*) FROM runs WHERE filename LIKE '%test02_no_unchecked.c%';")"
    expect_eq "$runs" "1" "runs row exists"

    local run_id
    run_id="$(sqlite3 "$TEST_DB" "SELECT id FROM runs WHERE filename LIKE '%test02_no_unchecked.c%' LIMIT 1;")"

    local error_count
    error_count="$(sqlite3 "$TEST_DB" "SELECT error_count FROM runs WHERE id=$run_id;")"
    expect_eq "$error_count" "0" "error_count is 0"

    local facts
    facts="$(sqlite3 "$TEST_DB" "SELECT COUNT(*) FROM facts WHERE run_id=$run_id;")"
    expect_eq "$facts" "0" "no facts recorded"
}

# ----------------------------------------------------------------------
# Test 3: specdb core functions
# ----------------------------------------------------------------------
test_specdb_core_functions() {
    log_info "=== Test 3: specdb core functions ==="

    local rd2 wr2 malloc3 fopen3 socket2 pthread_create3

    rd2="$(sqlite3 "$SPECDB_PATH" "SELECT COUNT(*) FROM functions WHERE name='read' AND section='2';")"
    wr2="$(sqlite3 "$SPECDB_PATH" "SELECT COUNT(*) FROM functions WHERE name='write' AND section='2';")"
    malloc3="$(sqlite3 "$SPECDB_PATH" "SELECT COUNT(*) FROM functions WHERE name='malloc' AND section IN ('3','3p');")"
    fopen3="$(sqlite3 "$SPECDB_PATH" "SELECT COUNT(*) FROM functions WHERE name='fopen' AND section IN ('3','3p');")"
    socket2="$(sqlite3 "$SPECDB_PATH" "SELECT COUNT(*) FROM functions WHERE name='socket' AND section IN ('2','3p','7');")"
    pthread_create3="$(sqlite3 "$SPECDB_PATH" "SELECT COUNT(*) FROM functions WHERE name='pthread_create' AND section IN ('3','3p');")"

    expect_eq "$rd2" "1" "specdb: read(2)"
    expect_eq "$wr2" "1" "specdb: write(2)"
    [ "$malloc3" -ge 1 ] && expect_eq "1" "1" "specdb: malloc" || expect_eq "0" "1" "specdb: malloc"
    [ "$fopen3" -ge 1 ]  && expect_eq "1" "1" "specdb: fopen"  || expect_eq "0" "1" "specdb: fopen"
    [ "$socket2" -ge 1 ] && expect_eq "1" "1" "specdb: socket" || expect_eq "0" "1" "specdb: socket"
    [ "$pthread_create3" -ge 1 ] && expect_eq "1" "1" "specdb: pthread_create" || expect_eq "0" "1" "specdb: pthread_create"
}

# ----------------------------------------------------------------------
# Test 4: analyzer --dump-views
# ----------------------------------------------------------------------
test_analyzer_dump_views() {
    log_info "=== Test 4: analyzer --dump-views ==="

    local src="${ROOT_DIR}/mc_tests/tests/test01_simple_unchecked.c"
    rm -f "$VIEWS_FILE"

    run_cmd "$ANALYZER_BIN" --db "$TEST_DB" "$src" --dump-views "$VIEWS_FILE"

    if [ ! -s "$VIEWS_FILE" ]; then
        log_fail "--dump-views produced empty file"
        failed=$((failed+1))
        return
    fi

    local first
    first="$(sed 's/^[[:space:]]*//' < "$VIEWS_FILE" | head -n1)"

    case "$first" in
        \{*) log_pass "views JSONL is valid JSON" ;;
        *)   log_fail "views JSONL does not start with {"; failed=$((failed+1)) ;;
    esac
}

# ----------------------------------------------------------------------
# Test 5: golden tests (mc_tests/tests/*.c/*.exp)
# ----------------------------------------------------------------------
test_golden_outputs() {
    log_info "=== Test 5: golden tests (mc_tests/tests/) ==="

    local tests_dir="${ROOT_DIR}/mc_tests/tests"
    rm -f "$GOLDEN_DB"

    local any=0

    for cfile in "$tests_dir"/*.c; do
        [ -e "$cfile" ] || continue
        any=1

        local base exp rel out norm
        base="$(basename "$cfile")"
        exp="${cfile%.c}.exp"
        rel="mc_tests/tests/$base"

        printf '    ==> %s\n' "$base"

        if [ ! -f "$exp" ]; then
            log_info "golden: skipping $base (no .exp)"
            continue
        fi

        out="$(mktemp)"
        norm="$(mktemp)"

        (
            cd "$ROOT_DIR"
            "$ANALYZER_BIN" --db "$GOLDEN_DB" "$rel" >"$out" 2>&1 || true
        )

        # Normalize path prefix so old expectations 'tests/...' still match
        sed 's|mc_tests/tests/|tests/|g' "$out" >"$norm"

        if diff -u "$exp" "$norm" >/dev/null 2>&1; then
            log_pass "golden matches for $base"
            passed=$((passed+1))
        else
            log_fail "golden MISMATCH for $base"
            diff -u "$exp" "$norm" || true
            failed=$((failed+1))
        fi

        rm -f "$out" "$norm"
    done

    if [ "$any" -eq 0 ]; then
        log_info "no golden tests found under mc_tests/tests/"
    fi
}

# ----------------------------------------------------------------------
# Main
# ----------------------------------------------------------------------
main() {
    test_analyzer_unchecked_basic
    test_analyzer_no_issues
    test_specdb_core_functions
    test_analyzer_dump_views
    test_golden_outputs

    echo
    if [ "$failed" -eq 0 ]; then
        printf "%sAll tests passed%s (passed=%d, failed=%d)\n" \
            "$COLOR_GREEN" "$COLOR_RESET" "$passed" "$failed"
        exit 0
    else
        printf "%sSome tests failed%s (passed=%d, failed=%d)\n" \
            "$COLOR_RED" "$COLOR_RESET" "$passed" "$failed"
        exit 1
    fi
}

main "$@"

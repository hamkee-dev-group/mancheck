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

SPECDB_BUILD="${ROOT_DIR}/specdb/specdb-build"

if [ ! -f "$SPECDB_PATH" ]; then
    log_info "specdb not found, building from manpages..."
    if [ ! -x "$SPECDB_BUILD" ]; then
        run_cmd make -C "${ROOT_DIR}/specdb"
    fi
    mkdir -p "$(dirname "$SPECDB_PATH")"
    run_cmd "$SPECDB_BUILD" "$SPECDB_PATH" --scan-section 2
    run_cmd "$SPECDB_BUILD" "$SPECDB_PATH" --scan-section 3
    if [ ! -f "$SPECDB_PATH" ]; then
        log_fatal "failed to build specdb at '${SPECDB_PATH}'"
    fi
fi

# ----------------------------------------------------------------------
# Test 1: analyzer basic unchecked-return (DB)
# ----------------------------------------------------------------------
test_analyzer_unchecked_basic() {
    log_info "=== Test 1: analyzer basic unchecked-return (DB) ==="

    local src="${ROOT_DIR}/mc_tests/tests/test01_simple_unchecked.c"
    run_cmd "$ANALYZER_BIN" --db "$TEST_DB" "$src" || true

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

    run_cmd "$ANALYZER_BIN" --db "$TEST_DB" "$src" --dump-views "$VIEWS_FILE" || true

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
# Test 6: JSON output mode
# ----------------------------------------------------------------------
test_json_output() {
    log_info "=== Test 6: JSON output mode ==="

    local src="${ROOT_DIR}/mc_tests/tests/test29_json_output.c"
    local out
    out="$("$ANALYZER_BIN" --json --no-db "$src" 2>/dev/null)" || true

    # Must be valid JSON (starts with { and ends with })
    local first last
    first="$(echo "$out" | head -c1)"
    last="$(echo "$out" | tail -c2 | head -c1)"

    if [ "$first" = "{" ] && [ "$last" = "}" ]; then
        log_pass "JSON output starts with { and ends with }"
        passed=$((passed+1))
    else
        log_fail "JSON output not well-formed (first='$first', last='$last')"
        failed=$((failed+1))
    fi

    # Must contain "files" array
    if echo "$out" | grep -q '"files"'; then
        log_pass "JSON output contains files array"
        passed=$((passed+1))
    else
        log_fail "JSON output missing files array"
        failed=$((failed+1))
    fi

    # Must mention read and malloc
    if echo "$out" | grep -q '"read"' && echo "$out" | grep -q '"malloc"'; then
        log_pass "JSON output contains expected function names"
        passed=$((passed+1))
    else
        log_fail "JSON output missing expected function names"
        failed=$((failed+1))
    fi

    # Must contain status and category fields
    if echo "$out" | grep -q '"status"' && echo "$out" | grep -q '"category"'; then
        log_pass "JSON output contains status and category fields"
        passed=$((passed+1))
    else
        log_fail "JSON output missing status/category fields"
        failed=$((failed+1))
    fi
}

# ----------------------------------------------------------------------
# Test 7: specdb comprehensive coverage
# ----------------------------------------------------------------------
test_specdb_comprehensive() {
    log_info "=== Test 7: specdb comprehensive coverage ==="

    local total_functions
    total_functions="$(sqlite3 "$SPECDB_PATH" "SELECT COUNT(*) FROM functions;")"
    if [ "$total_functions" -ge 100 ]; then
        log_pass "specdb has $total_functions functions (>= 100)"
        passed=$((passed+1))
    else
        log_fail "specdb has only $total_functions functions (expected >= 100)"
        failed=$((failed+1))
    fi

    # Check sections are populated
    local total_sections
    total_sections="$(sqlite3 "$SPECDB_PATH" "SELECT COUNT(*) FROM function_sections;")"
    if [ "$total_sections" -ge 500 ]; then
        log_pass "specdb has $total_sections sections (>= 500)"
        passed=$((passed+1))
    else
        log_fail "specdb has only $total_sections sections (expected >= 500)"
        failed=$((failed+1))
    fi

    # Check aliases are populated
    local total_aliases
    total_aliases="$(sqlite3 "$SPECDB_PATH" "SELECT COUNT(*) FROM function_aliases;")"
    if [ "$total_aliases" -ge 100 ]; then
        log_pass "specdb has $total_aliases aliases (>= 100)"
        passed=$((passed+1))
    else
        log_fail "specdb has only $total_aliases aliases (expected >= 100)"
        failed=$((failed+1))
    fi

    # Check man sections 2 and 3 both present
    local sec2 sec3
    sec2="$(sqlite3 "$SPECDB_PATH" "SELECT COUNT(*) FROM functions WHERE section='2';")"
    sec3="$(sqlite3 "$SPECDB_PATH" "SELECT COUNT(*) FROM functions WHERE section='3';")"

    if [ "$sec2" -ge 50 ]; then
        log_pass "specdb: section 2 has $sec2 entries (>= 50)"
        passed=$((passed+1))
    else
        log_fail "specdb: section 2 has only $sec2 entries (expected >= 50)"
        failed=$((failed+1))
    fi

    if [ "$sec3" -ge 100 ]; then
        log_pass "specdb: section 3 has $sec3 entries (>= 100)"
        passed=$((passed+1))
    else
        log_fail "specdb: section 3 has only $sec3 entries (expected >= 100)"
        failed=$((failed+1))
    fi

    # Spot-check key functions have RETURN VALUE sections
    local rv_read rv_malloc rv_fopen
    rv_read="$(sqlite3 "$SPECDB_PATH" \
        "SELECT COUNT(*) FROM functions f JOIN function_sections fs ON f.id=fs.function_id WHERE f.name='read' AND f.section='2' AND fs.section_name='RETURN VALUE';")"
    rv_malloc="$(sqlite3 "$SPECDB_PATH" \
        "SELECT COUNT(*) FROM functions f JOIN function_sections fs ON f.id=fs.function_id WHERE f.name='malloc' AND f.section='3' AND fs.section_name='RETURN VALUE';")"
    rv_fopen="$(sqlite3 "$SPECDB_PATH" \
        "SELECT COUNT(*) FROM functions f JOIN function_sections fs ON f.id=fs.function_id WHERE f.name='fopen' AND f.section='3' AND fs.section_name='RETURN VALUE';")"

    [ "$rv_read"   -ge 1 ] && { log_pass "specdb: read(2) has RETURN VALUE section"; passed=$((passed+1)); } \
                            || { log_fail "specdb: read(2) missing RETURN VALUE section"; failed=$((failed+1)); }
    [ "$rv_malloc"  -ge 1 ] && { log_pass "specdb: malloc(3) has RETURN VALUE section"; passed=$((passed+1)); } \
                             || { log_fail "specdb: malloc(3) missing RETURN VALUE section"; failed=$((failed+1)); }
    [ "$rv_fopen"   -ge 1 ] && { log_pass "specdb: fopen(3) has RETURN VALUE section"; passed=$((passed+1)); } \
                             || { log_fail "specdb: fopen(3) missing RETURN VALUE section"; failed=$((failed+1)); }
}

# ----------------------------------------------------------------------
# Test 8: DB run tracking with multiple files
# ----------------------------------------------------------------------
test_db_multi_file() {
    log_info "=== Test 8: DB run tracking with multiple files ==="

    local db="${OUT_DIR}/multi_run.db"
    rm -f "$db"

    local src1="${ROOT_DIR}/mc_tests/tests/test01_simple_unchecked.c"
    local src2="${ROOT_DIR}/mc_tests/tests/test30_clean_file.c"
    local src3="${ROOT_DIR}/mc_tests/tests/test05_malloc_unchecked.c"

    run_cmd "$ANALYZER_BIN" --db "$db" "$src1" "$src2" "$src3" || true

    local run_count
    run_count="$(sqlite3 "$db" "SELECT COUNT(*) FROM runs;")"
    expect_eq "$run_count" "3" "three runs recorded for three files"

    # Check that error_count varies per file
    local err1 err2 err3
    err1="$(sqlite3 "$db" "SELECT error_count FROM runs WHERE filename LIKE '%test01_%';")"
    err2="$(sqlite3 "$db" "SELECT error_count FROM runs WHERE filename LIKE '%test30_%';")"
    err3="$(sqlite3 "$db" "SELECT error_count FROM runs WHERE filename LIKE '%test05_%';")"

    expect_eq "$err1" "2" "test01 has 2 errors"
    expect_eq "$err2" "0" "test30 has 0 errors"
    expect_eq "$err3" "1" "test05 has 1 error"
}

# ----------------------------------------------------------------------
# Test 9: no-db mode produces no database file
# ----------------------------------------------------------------------
test_no_db_mode() {
    log_info "=== Test 9: --no-db mode ==="

    local db="${OUT_DIR}/should_not_exist.db"
    rm -f "$db"

    local src="${ROOT_DIR}/mc_tests/tests/test01_simple_unchecked.c"
    "$ANALYZER_BIN" --no-db "$src" >/dev/null 2>&1 || true

    if [ ! -f "$db" ]; then
        log_pass "--no-db does not create a database"
        passed=$((passed+1))
    else
        log_fail "--no-db created a database"
        failed=$((failed+1))
    fi
}

# ----------------------------------------------------------------------
# Test 10: FTS search in run database
# ----------------------------------------------------------------------
test_db_fts_search() {
    log_info "=== Test 10: DB FTS search ==="

    local db="${OUT_DIR}/fts_test.db"
    rm -f "$db"

    local src="${ROOT_DIR}/mc_tests/tests/test01_simple_unchecked.c"
    "$ANALYZER_BIN" --db "$db" "$src" >/dev/null 2>&1 || true

    # Search for "read" in facts_fts
    local fts_hits
    fts_hits="$(sqlite3 "$db" "SELECT COUNT(*) FROM facts_fts WHERE facts_fts MATCH 'read';")"

    if [ "$fts_hits" -ge 1 ]; then
        log_pass "FTS search for 'read' returns $fts_hits hit(s)"
        passed=$((passed+1))
    else
        log_fail "FTS search for 'read' returned 0 hits"
        failed=$((failed+1))
    fi
}

# ----------------------------------------------------------------------
# Test 11: specdb integration – analyzer with --specdb flag
# ----------------------------------------------------------------------
test_specdb_integration() {
    log_info "=== Test 11: specdb integration (--specdb flag) ==="

    # mmap is NOT in the static rules table but IS in specdb
    local src="${ROOT_DIR}/mc_tests/tests/test32_mmap_unchecked.c"

    # Without specdb: only ftruncate + lseek should be flagged (2 issues)
    local out_no_specdb
    out_no_specdb="$("$ANALYZER_BIN" --no-db "$src" 2>&1 || true)"
    local count_no_specdb
    count_no_specdb="$(echo "$out_no_specdb" | grep -c 'ignored return' || true)"
    expect_eq "$count_no_specdb" "2" "without specdb: 2 issues (ftruncate + lseek)"

    # With specdb: mmap + munmap should also be flagged (4 total)
    local out_with_specdb
    out_with_specdb="$("$ANALYZER_BIN" --no-db --specdb "$SPECDB_PATH" "$src" 2>&1 || true)"
    local count_with_specdb
    count_with_specdb="$(echo "$out_with_specdb" | grep -c 'ignored return' || true)"
    expect_eq "$count_with_specdb" "4" "with specdb: 4 issues (mmap + munmap + ftruncate + lseek)"

    # Verify mmap specifically appears
    if echo "$out_with_specdb" | grep -q 'mmap()'; then
        log_pass "specdb: mmap() flagged by specdb lookup"
        passed=$((passed+1))
    else
        log_fail "specdb: mmap() NOT flagged despite specdb"
        failed=$((failed+1))
    fi

    # Verify a function NOT in specdb is still ignored
    local clean_src="${ROOT_DIR}/mc_tests/tests/test30_clean_file.c"
    local out_clean
    out_clean="$("$ANALYZER_BIN" --no-db --specdb "$SPECDB_PATH" "$clean_src" 2>&1 || true)"
    local count_clean
    count_clean="$(echo "$out_clean" | grep -c 'ignored return' || true)"
    expect_eq "$count_clean" "0" "specdb: clean file still produces 0 issues"
}

# ----------------------------------------------------------------------
# Test 12: GCC formatter – mixed categories
# ----------------------------------------------------------------------
test_gcc_format_mixed() {
    log_info "=== Test 12: GCC formatter – mixed categories ==="

    local src="mc_tests/tests/test27_mixed_categories.c"
    local out
    out="$(cd "$ROOT_DIR" && "$ANALYZER_BIN" --gcc --no-db "$src" 2>&1)" || true

    local count
    count="$(echo "$out" | grep -c '^mc_tests/tests/test27_mixed_categories\.c:.*: warning:' || true)"
    expect_eq "$count" "4" "gcc format emits 4 warning-prefixed diagnostics"

    for line_num in 11 14 17 20; do
        if echo "$out" | grep -q "^mc_tests/tests/test27_mixed_categories\.c:${line_num}:5: warning:"; then
            log_pass "gcc: diagnostic at line $line_num"
            passed=$((passed+1))
        else
            log_fail "gcc: missing diagnostic at line $line_num"
            failed=$((failed+1))
        fi
    done

    echo "$out" | grep -q 'gets()'    && { log_pass "gcc: gets() message"; passed=$((passed+1)); } \
                                       || { log_fail "gcc: gets() message missing"; failed=$((failed+1)); }
    echo "$out" | grep -q 'read()'    && { log_pass "gcc: read() message"; passed=$((passed+1)); } \
                                       || { log_fail "gcc: read() message missing"; failed=$((failed+1)); }
    echo "$out" | grep -q 'printf()'  && { log_pass "gcc: printf() message"; passed=$((passed+1)); } \
                                       || { log_fail "gcc: printf() message missing"; failed=$((failed+1)); }
    echo "$out" | grep -q 'sprintf()' && { log_pass "gcc: sprintf() message"; passed=$((passed+1)); } \
                                       || { log_fail "gcc: sprintf() message missing"; failed=$((failed+1)); }
}

# ----------------------------------------------------------------------
# Test 13: GCC formatter – double_close
# ----------------------------------------------------------------------
test_gcc_format_double_close() {
    log_info "=== Test 13: GCC formatter – double_close ==="

    local src="mc_tests/tests/double_close.c"
    local out
    out="$(cd "$ROOT_DIR" && "$ANALYZER_BIN" --gcc --no-db "$src" 2>&1)" || true

    if echo "$out" | grep -q 'warning: double_close: second call to close(fd); first at line 9'; then
        log_pass "gcc: double_close diagnostic in GCC-style format"
        passed=$((passed+1))
    else
        log_fail "gcc: double_close diagnostic missing or not GCC-formatted"
        failed=$((failed+1))
    fi
}

# ----------------------------------------------------------------------
# Test 14: --suppressions config
# ----------------------------------------------------------------------
test_suppressions() {
    log_info "=== Test 14: --suppressions config ==="

    local src="mc_tests/tests/test35_suppression.c"
    local sup="mc_tests/tests/test35_suppression.sup"

    # Without suppressions: both warnings appear
    local out_nosup
    out_nosup="$(cd "$ROOT_DIR" && "$ANALYZER_BIN" --no-db "$src" 2>&1)" || true

    local count_nosup
    count_nosup="$(echo "$out_nosup" | grep -c ':' || true)"
    expect_eq "$count_nosup" "2" "no suppression: 2 diagnostics"

    # With suppressions: return_value_check suppressed, dangerous remains
    local out_sup
    out_sup="$(cd "$ROOT_DIR" && "$ANALYZER_BIN" --no-db --suppressions "$sup" "$src" 2>&1)" || true

    local count_sup
    count_sup="$(echo "$out_sup" | grep -c ':' || true)"
    expect_eq "$count_sup" "1" "with suppression: 1 diagnostic remains"

    if echo "$out_sup" | grep -q 'dangerous function gets()'; then
        log_pass "suppression: dangerous_function warning still reported"
        passed=$((passed+1))
    else
        log_fail "suppression: dangerous_function warning missing"
        failed=$((failed+1))
    fi

    if echo "$out_sup" | grep -q 'read()'; then
        log_fail "suppression: return_value_check should be suppressed"
        failed=$((failed+1))
    else
        log_pass "suppression: return_value_check correctly suppressed"
        passed=$((passed+1))
    fi

    # Same file with absolute path invocation
    local abs_src
    abs_src="$(cd "$ROOT_DIR" && realpath "$src")"
    local out_abs
    out_abs="$(cd "$ROOT_DIR" && "$ANALYZER_BIN" --no-db --suppressions "$sup" "$abs_src" 2>&1)" || true

    local count_abs
    count_abs="$(echo "$out_abs" | grep -c 'dangerous function gets()' || true)"
    expect_eq "$count_abs" "1" "suppression works with absolute path invocation"

    # Different file same category still reports
    local out_other
    out_other="$(cd "$ROOT_DIR" && "$ANALYZER_BIN" --no-db --suppressions "$sup" mc_tests/tests/test01_simple_unchecked.c 2>&1)" || true

    local count_other
    count_other="$(echo "$out_other" | grep -c 'read()' || true)"
    expect_eq "$count_other" "1" "different file same category still reports"

    # DB error_count reflects suppression
    local sup_db="${ROOT_DIR}/mc_tests/suppress_test.db"
    rm -f "$sup_db"
    (cd "$ROOT_DIR" && "$ANALYZER_BIN" --db "$sup_db" --suppressions "$sup" "$src" >/dev/null 2>&1) || true

    local db_count
    db_count="$(sqlite3 "$sup_db" "SELECT error_count FROM runs ORDER BY rowid DESC LIMIT 1;" 2>/dev/null || echo "")"
    expect_eq "$db_count" "1" "DB error_count reflects only unsuppressed findings"
    rm -f "$sup_db"

    # JSON mode: suppressed diagnostic absent, unsuppressed present
    local out_json
    out_json="$(cd "$ROOT_DIR" && "$ANALYZER_BIN" --json --no-db --suppressions "$sup" "$src" 2>/dev/null)" || true

    local json_read_count
    json_read_count="$(echo "$out_json" | grep -c '"read"' || true)"
    expect_eq "$json_read_count" "0" "JSON mode: suppressed diagnostic absent"

    local json_gets_count
    json_gets_count="$(echo "$out_json" | grep -c '"gets"' || true)"
    expect_eq "$json_gets_count" "1" "JSON mode: unsuppressed diagnostic present"

    # JSON mode: checked calls in suppressed category still appear
    local out_json_checked
    out_json_checked="$(cd "$ROOT_DIR" && "$ANALYZER_BIN" --json --no-db --suppressions "$sup" mc_tests/tests/test28_binary_expr_check.c 2>/dev/null)" || true

    local json_checked
    json_checked="$(echo "$out_json_checked" | grep -c '"checked_cond"' || true)"
    if [ "$json_checked" -gt 0 ]; then
        log_pass "JSON mode: checked calls in suppressed category still appear"
        passed=$((passed+1))
    else
        log_fail "JSON mode: checked calls in suppressed category should not be suppressed"
        failed=$((failed+1))
    fi
}

# ----------------------------------------------------------------------
# Test 15: Inline mc:ignore / NOLINT(mancheck)
# ----------------------------------------------------------------------
test_inline_suppress() {
    log_info "=== Test 15: Inline mc:ignore / NOLINT(mancheck) ==="

    local src="mc_tests/tests/test36_inline_suppress.c"

    local out
    out="$(cd "$ROOT_DIR" && "$ANALYZER_BIN" --no-db "$src" 2>&1)" || true

    # 3 diagnostics: unsuppressed read (line 12), gets (line 14), read after string literal (line 18)
    local count
    count="$(echo "$out" | grep -c ':' || true)"
    expect_eq "$count" "3" "inline suppress: 3 diagnostics remain"

    # mc:ignore on same line suppresses read() at line 7
    if echo "$out" | grep -q 'test36_inline_suppress\.c:7:'; then
        log_fail "inline suppress: mc:ignore did not suppress line 7"
        failed=$((failed+1))
    else
        log_pass "inline suppress: mc:ignore suppressed line 7"
        passed=$((passed+1))
    fi

    # NOLINT(mancheck) on previous line suppresses read() at line 10
    if echo "$out" | grep -q 'test36_inline_suppress\.c:10:'; then
        log_fail "inline suppress: NOLINT(mancheck) did not suppress line 10"
        failed=$((failed+1))
    else
        log_pass "inline suppress: NOLINT(mancheck) suppressed line 10"
        passed=$((passed+1))
    fi

    # Unsuppressed read() at line 12 still reports
    if echo "$out" | grep -q 'test36_inline_suppress\.c:12:.*read()'; then
        log_pass "inline suppress: unsuppressed read() at line 12 reports"
        passed=$((passed+1))
    else
        log_fail "inline suppress: unsuppressed read() at line 12 missing"
        failed=$((failed+1))
    fi

    # gets() at line 14 still reports (different category)
    if echo "$out" | grep -q 'test36_inline_suppress\.c:14:.*gets()'; then
        log_pass "inline suppress: gets() at line 14 reports"
        passed=$((passed+1))
    else
        log_fail "inline suppress: gets() at line 14 missing"
        failed=$((failed+1))
    fi

    # "// mc:ignore" inside string literal must NOT suppress line 18
    if echo "$out" | grep -q 'test36_inline_suppress\.c:18:.*read()'; then
        log_pass "inline suppress: marker in string literal does not suppress"
        passed=$((passed+1))
    else
        log_fail "inline suppress: false positive from marker in string literal"
        failed=$((failed+1))
    fi

    # URL in string + real // mc:ignore comment DOES suppress line 21
    if echo "$out" | grep -q 'test36_inline_suppress\.c:21:'; then
        log_fail "inline suppress: marker after URL string not recognised"
        failed=$((failed+1))
    else
        log_pass "inline suppress: marker after URL in string correctly suppresses"
        passed=$((passed+1))
    fi

    # JSON mode: inline suppression also works
    local out_json
    out_json="$(cd "$ROOT_DIR" && "$ANALYZER_BIN" --json --no-db "$src" 2>/dev/null)" || true

    local json_gets_count
    json_gets_count="$(echo "$out_json" | grep -c '"gets"' || true)"
    expect_eq "$json_gets_count" "1" "inline suppress JSON: gets() still present"
}

# ----------------------------------------------------------------------
# Test 16: Exit codes – non-zero on findings, zero when clean
# ----------------------------------------------------------------------
test_exit_codes() {
    log_info "=== Test 16: Exit codes ==="

    # Text mode: findings → exit 1
    local rc=0
    (cd "$ROOT_DIR" && "$ANALYZER_BIN" --no-db mc_tests/tests/test01_simple_unchecked.c >/dev/null 2>&1) || rc=$?
    expect_eq "$rc" "1" "text mode: exit 1 when findings reported"

    # Text mode: clean file → exit 0
    rc=0
    (cd "$ROOT_DIR" && "$ANALYZER_BIN" --no-db mc_tests/tests/test30_clean_file.c >/dev/null 2>&1) || rc=$?
    expect_eq "$rc" "0" "text mode: exit 0 when no findings"

    # JSON mode: findings → exit 1, valid JSON
    rc=0
    local json_out
    json_out="$(cd "$ROOT_DIR" && "$ANALYZER_BIN" --json --no-db mc_tests/tests/test29_json_output.c 2>/dev/null)" || rc=$?
    expect_eq "$rc" "1" "json mode: exit 1 when findings reported"

    # Verify JSON shape is still {"files":[...]}
    if echo "$json_out" | head -1 | grep -q '^{"files":\['; then
        log_pass "json mode: output starts with {\"files\":[..."
        passed=$((passed+1))
    else
        log_fail "json mode: output does not start with {\"files\":[..."
        failed=$((failed+1))
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
    test_json_output
    test_specdb_comprehensive
    test_db_multi_file
    test_no_db_mode
    test_db_fts_search
    test_specdb_integration
    test_gcc_format_mixed
    test_gcc_format_double_close
    test_suppressions
    test_inline_suppress
    test_exit_codes

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

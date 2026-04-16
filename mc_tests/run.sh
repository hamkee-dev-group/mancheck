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
need_tool clang

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
# Test 22: GCC stream split – diagnostics to stderr, stdout empty
# ----------------------------------------------------------------------
test_gcc_stream_split() {
    log_info "=== Test 22: GCC stream split ==="

    local rc tmpout tmperr

    # --- test27_mixed_categories.c ---
    tmpout="$(mktemp)"
    tmperr="$(mktemp)"
    rc=0
    (cd "$ROOT_DIR" && "$ANALYZER_BIN" --gcc --no-db mc_tests/tests/test27_mixed_categories.c) \
        >"$tmpout" 2>"$tmperr" || rc=$?

    expect_eq "$rc" "1" "gcc-split test27: exit 1"

    if [ ! -s "$tmpout" ]; then
        log_pass "gcc-split test27: stdout empty"
        passed=$((passed+1))
    else
        log_fail "gcc-split test27: stdout not empty"
        failed=$((failed+1))
    fi

    local err_lines
    err_lines="$(wc -l < "$tmperr")"
    expect_eq "$err_lines" "4" "gcc-split test27: stderr has 4 lines"

    for line_num in 11 14 17 20; do
        if grep -q "^mc_tests/tests/test27_mixed_categories\.c:${line_num}:5: warning:" "$tmperr"; then
            log_pass "gcc-split test27: diagnostic at line $line_num"
            passed=$((passed+1))
        else
            log_fail "gcc-split test27: missing diagnostic at line $line_num"
            failed=$((failed+1))
        fi
    done

    grep -q 'gets()'    "$tmperr" && { log_pass "gcc-split test27: gets()";    passed=$((passed+1)); } \
                                   || { log_fail "gcc-split test27: gets()";    failed=$((failed+1)); }
    grep -q 'read()'    "$tmperr" && { log_pass "gcc-split test27: read()";    passed=$((passed+1)); } \
                                   || { log_fail "gcc-split test27: read()";    failed=$((failed+1)); }
    grep -q 'printf()'  "$tmperr" && { log_pass "gcc-split test27: printf()";  passed=$((passed+1)); } \
                                   || { log_fail "gcc-split test27: printf()";  failed=$((failed+1)); }
    grep -q 'sprintf()' "$tmperr" && { log_pass "gcc-split test27: sprintf()"; passed=$((passed+1)); } \
                                   || { log_fail "gcc-split test27: sprintf()"; failed=$((failed+1)); }

    rm -f "$tmpout" "$tmperr"

    # --- double_close.c ---
    tmpout="$(mktemp)"
    tmperr="$(mktemp)"
    rc=0
    (cd "$ROOT_DIR" && "$ANALYZER_BIN" --gcc --no-db mc_tests/tests/double_close.c) \
        >"$tmpout" 2>"$tmperr" || rc=$?

    expect_eq "$rc" "1" "gcc-split double_close: exit 1"

    if [ ! -s "$tmpout" ]; then
        log_pass "gcc-split double_close: stdout empty"
        passed=$((passed+1))
    else
        log_fail "gcc-split double_close: stdout not empty"
        failed=$((failed+1))
    fi

    err_lines="$(wc -l < "$tmperr")"
    expect_eq "$err_lines" "4" "gcc-split double_close: stderr has 4 lines"

    local close_count
    close_count="$(grep -c 'ignored return of close()' "$tmperr" || true)"
    expect_eq "$close_count" "2" "gcc-split double_close: 2 ignored-return-of-close diagnostics"

    local dc_count
    dc_count="$(grep -c 'double_close:' "$tmperr" || true)"
    expect_eq "$dc_count" "2" "gcc-split double_close: 2 double_close diagnostics"

    grep -q 'second call to close(fd); first at line 9'  "$tmperr" \
        && { log_pass "gcc-split double_close: close(fd) detail"; passed=$((passed+1)); } \
        || { log_fail "gcc-split double_close: close(fd) detail"; failed=$((failed+1)); }
    grep -q 'second call to free(p); first at line 12'   "$tmperr" \
        && { log_pass "gcc-split double_close: free(p) detail";  passed=$((passed+1)); } \
        || { log_fail "gcc-split double_close: free(p) detail";  failed=$((failed+1)); }

    rm -f "$tmpout" "$tmperr"

    # --- test30_clean_file.c ---
    tmpout="$(mktemp)"
    tmperr="$(mktemp)"
    rc=0
    (cd "$ROOT_DIR" && "$ANALYZER_BIN" --gcc --no-db mc_tests/tests/test30_clean_file.c) \
        >"$tmpout" 2>"$tmperr" || rc=$?

    expect_eq "$rc" "0" "gcc-split clean: exit 0"

    if [ ! -s "$tmpout" ]; then
        log_pass "gcc-split clean: stdout empty"
        passed=$((passed+1))
    else
        log_fail "gcc-split clean: stdout not empty"
        failed=$((failed+1))
    fi

    if [ ! -s "$tmperr" ]; then
        log_pass "gcc-split clean: stderr empty"
        passed=$((passed+1))
    else
        log_fail "gcc-split clean: stderr not empty"
        failed=$((failed+1))
    fi

    rm -f "$tmpout" "$tmperr"
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
# Test 15b: Comment-only inline suppressions are consumed per diagnostic
# ----------------------------------------------------------------------
test_inline_suppress_comment_only_chain() {
    log_info "=== Test 15b: comment-only inline suppressions bind once ==="

    local src="mc_tests/tests/test38_inline_comment_only_chain.c"
    local out
    out="$(cd "$ROOT_DIR" && "$ANALYZER_BIN" --no-db "$src" 2>&1)" || true

    local count
    count="$(echo "$out" | grep -c ':' || true)"
    expect_eq "$count" "1" "comment-only inline suppress: later diagnostic still reports"

    if echo "$out" | grep -q 'test38_inline_comment_only_chain\.c:9:'; then
        log_fail "comment-only inline suppress: repeated markers leaked past first diagnostic"
        failed=$((failed+1))
    else
        log_pass "comment-only inline suppress: repeated markers consumed by first diagnostic"
        passed=$((passed+1))
    fi

    if echo "$out" | grep -q 'test38_inline_comment_only_chain\.c:11:.*gets()'; then
        log_pass "comment-only inline suppress: later diagnostic reports"
        passed=$((passed+1))
    else
        log_fail "comment-only inline suppress: later diagnostic missing"
        failed=$((failed+1))
    fi

    local out_json
    out_json="$(cd "$ROOT_DIR" && "$ANALYZER_BIN" --json --no-db "$src" 2>/dev/null)" || true

    if echo "$out_json" | grep -q '"line":9'; then
        log_fail "comment-only inline suppress JSON: suppressed diagnostic still present"
        failed=$((failed+1))
    else
        log_pass "comment-only inline suppress JSON: first diagnostic suppressed"
        passed=$((passed+1))
    fi

    if echo "$out_json" | grep -q '"line":11'; then
        log_pass "comment-only inline suppress JSON: later diagnostic preserved"
        passed=$((passed+1))
    else
        log_fail "comment-only inline suppress JSON: later diagnostic missing"
        failed=$((failed+1))
    fi

    local json_issue_count
    json_issue_count="$(echo "$out_json" | grep -o '"issue_count":[0-9]*' | head -n1 | cut -d: -f2)"
    expect_eq "$json_issue_count" "1" "comment-only inline suppress JSON: issue_count is 1"
}

# ----------------------------------------------------------------------
# Test 15c: NOLINT(mancheck) next-warning and spaced same-line forms
# ----------------------------------------------------------------------
test_inline_suppress_nolint_next_warn() {
    log_info "=== Test 15c: NOLINT next-warning and spaced same-line ==="

    local src="mc_tests/tests/test39_nolint_next_warn.c"
    local out
    out="$(cd "$ROOT_DIR" && "$ANALYZER_BIN" --no-db "$src" 2>&1)" || true

    local count
    count="$(echo "$out" | grep -c ':' || true)"
    expect_eq "$count" "2" "NOLINT next-warning: 2 diagnostics remain"

    if echo "$out" | grep -q 'test39_nolint_next_warn\.c:10:'; then
        log_fail "NOLINT next-warning: comment-only marker did not suppress line 10"
        failed=$((failed+1))
    else
        log_pass "NOLINT next-warning: comment-only marker suppressed line 10"
        passed=$((passed+1))
    fi

    if echo "$out" | grep -q 'test39_nolint_next_warn\.c:11:.*read()'; then
        log_pass "NOLINT next-warning: later read() at line 11 reports"
        passed=$((passed+1))
    else
        log_fail "NOLINT next-warning: later read() at line 11 missing"
        failed=$((failed+1))
    fi

    if echo "$out" | grep -q 'test39_nolint_next_warn\.c:12:'; then
        log_fail "NOLINT next-warning: spaced same-line marker did not suppress line 12"
        failed=$((failed+1))
    else
        log_pass "NOLINT next-warning: spaced same-line marker suppressed line 12"
        passed=$((passed+1))
    fi

    if echo "$out" | grep -q 'test39_nolint_next_warn\.c:13:.*gets()'; then
        log_pass "NOLINT next-warning: gets() at line 13 reports"
        passed=$((passed+1))
    else
        log_fail "NOLINT next-warning: gets() at line 13 missing"
        failed=$((failed+1))
    fi

    local out_json
    out_json="$(cd "$ROOT_DIR" && "$ANALYZER_BIN" --json --no-db "$src" 2>/dev/null)" || true

    if echo "$out_json" | grep -q '"line":10'; then
        log_fail "NOLINT next-warning JSON: suppressed line 10 still present"
        failed=$((failed+1))
    else
        log_pass "NOLINT next-warning JSON: line 10 suppressed"
        passed=$((passed+1))
    fi

    if echo "$out_json" | grep -q '"line":11'; then
        log_pass "NOLINT next-warning JSON: line 11 preserved"
        passed=$((passed+1))
    else
        log_fail "NOLINT next-warning JSON: line 11 missing"
        failed=$((failed+1))
    fi

    if echo "$out_json" | grep -q '"line":12'; then
        log_fail "NOLINT next-warning JSON: suppressed line 12 still present"
        failed=$((failed+1))
    else
        log_pass "NOLINT next-warning JSON: line 12 suppressed"
        passed=$((passed+1))
    fi

    if echo "$out_json" | grep -q '"line":13'; then
        log_pass "NOLINT next-warning JSON: line 13 preserved"
        passed=$((passed+1))
    else
        log_fail "NOLINT next-warning JSON: line 13 missing"
        failed=$((failed+1))
    fi

    local json_issue_count
    json_issue_count="$(echo "$out_json" | grep -o '"issue_count":[0-9]*' | head -n1 | cut -d: -f2)"
    expect_eq "$json_issue_count" "2" "NOLINT next-warning JSON: issue_count is 2"
}

# ----------------------------------------------------------------------
# Test 15d: Same-line mc:ignore suppresses only one diagnostic
# ----------------------------------------------------------------------
test_inline_suppress_same_line_scope() {
    log_info "=== Test 15d: same-line mc:ignore suppresses one diagnostic ==="

    local src="mc_tests/tests/test40_suppress_scope.c"
    local out
    out="$(cd "$ROOT_DIR" && "$ANALYZER_BIN" --no-db "$src" 2>&1)" || true

    local count
    count="$(echo "$out" | grep -c '^mc_tests/tests/test40_suppress_scope\.c:' || true)"
    expect_eq "$count" "1" "same-line inline suppress: exactly one diagnostic remains"

    if echo "$out" | grep -q 'test40_suppress_scope\.c:7:.*read()'; then
        log_fail "same-line inline suppress: first diagnostic should be suppressed"
        failed=$((failed+1))
    else
        log_pass "same-line inline suppress: first diagnostic consumed by marker"
        passed=$((passed+1))
    fi

    if echo "$out" | grep -q 'test40_suppress_scope\.c:7:.*gets()'; then
        log_pass "same-line inline suppress: second diagnostic still reports"
        passed=$((passed+1))
    else
        log_fail "same-line inline suppress: second diagnostic missing"
        failed=$((failed+1))
    fi

    local out_json
    out_json="$(cd "$ROOT_DIR" && "$ANALYZER_BIN" --json --no-db "$src" 2>/dev/null)" || true

    local json_issue_count
    json_issue_count="$(echo "$out_json" | grep -o '"issue_count":[0-9]*' | head -n1 | cut -d: -f2)"
    expect_eq "$json_issue_count" "1" "same-line inline suppress JSON: issue_count is 1"

    if echo "$out_json" | grep -q '"function":"read"'; then
        log_fail "same-line inline suppress JSON: suppressed diagnostic still present"
        failed=$((failed+1))
    else
        log_pass "same-line inline suppress JSON: suppressed diagnostic absent"
        passed=$((passed+1))
    fi

    if echo "$out_json" | grep -q '"function":"gets"'; then
        log_pass "same-line inline suppress JSON: remaining diagnostic preserved"
        passed=$((passed+1))
    else
        log_fail "same-line inline suppress JSON: remaining diagnostic missing"
        failed=$((failed+1))
    fi
}

# ----------------------------------------------------------------------
# Test 16a: SARIF output mode
# ----------------------------------------------------------------------
test_sarif_output() {
    log_info "=== Test 16a: SARIF output mode ==="

    local src="mc_tests/tests/test16_dangerous_functions.c"
    local clean_src="mc_tests/tests/test30_clean_file.c"

    local out
    out="$(cd "$ROOT_DIR" && "$ANALYZER_BIN" --sarif --no-db "$src" 2>/dev/null)" || true

    if echo "$out" | grep -q '^{"version":"2.1.0","runs":\[{"tool":{"driver":{"name":"mancheck"}},"results":\['; then
        log_pass "SARIF output has expected top-level structure"
        passed=$((passed+1))
    else
        log_fail "SARIF output missing expected top-level structure"
        failed=$((failed+1))
    fi

    local result_count
    result_count="$(echo "$out" | grep -c '"ruleId"' || true)"
    expect_eq "$result_count" "7" "SARIF output reports all dangerous-function findings"

    if echo "$out" | grep -q '"uri":"mc_tests/tests/test16_dangerous_functions.c"' &&
       echo "$out" | grep -q 'dangerous function gets()'; then
        log_pass "SARIF output includes location and message text"
        passed=$((passed+1))
    else
        log_fail "SARIF output missing expected location or message text"
        failed=$((failed+1))
    fi

    local rc=0
    (cd "$ROOT_DIR" && "$ANALYZER_BIN" --warn-exit --sarif --no-db "$src" >/dev/null 2>&1) || rc=$?
    expect_eq "$rc" "1" "--warn-exit sarif mode: exit 1 when findings reported"

    rc=0
    (cd "$ROOT_DIR" && "$ANALYZER_BIN" --warn-exit --sarif --no-db "$clean_src" >/dev/null 2>&1) || rc=$?
    expect_eq "$rc" "0" "--warn-exit sarif mode: exit 0 when no findings"

    rc=0
    local combo_out
    combo_out="$(cd "$ROOT_DIR" && "$ANALYZER_BIN" --json --sarif --no-db "$src" 2>&1)" || rc=$?
    expect_eq "$rc" "1" "--json and --sarif cannot be used together"

    if echo "$combo_out" | grep -q -- '--json and --sarif cannot be used together'; then
        log_pass "incompatible JSON/SARIF flags produce a clear error"
        passed=$((passed+1))
    else
        log_fail "incompatible JSON/SARIF flags missing clear error"
        failed=$((failed+1))
    fi

    local fail_db="${OUT_DIR}/sarif_fail.db"
    rm -f "$fail_db"

    rc=0
    local fail_out
    fail_out="$(cd "$ROOT_DIR" && MANCHECK_SARIF_FAIL_AT=1 "$ANALYZER_BIN" --sarif --db "$fail_db" "$src" 2>&1)" || rc=$?
    expect_eq "$rc" "1" "SARIF collector failure exits non-zero"

    local fail_result_count
    fail_result_count="$(echo "$fail_out" | grep -c '"ruleId"' || true)"
    expect_eq "$fail_result_count" "0" "SARIF collector failure emits no partial results"

    local fail_db_count
    fail_db_count="$(sqlite3 "$fail_db" "SELECT error_count FROM runs ORDER BY rowid DESC LIMIT 1;" 2>/dev/null || echo "")"
    expect_eq "$fail_db_count" "1" "SARIF collector failure marks DB run as non-success"
}

# ----------------------------------------------------------------------
# Test 16: --warn-exit flag
# ----------------------------------------------------------------------
test_warn_exit() {
    log_info "=== Test 16: --warn-exit flag ==="

    # Without --warn-exit: findings → exit 0
    local rc=0
    (cd "$ROOT_DIR" && "$ANALYZER_BIN" --no-db mc_tests/tests/test16_dangerous_functions.c >/dev/null 2>&1) || rc=$?
    expect_eq "$rc" "0" "no --warn-exit: exit 0 despite findings"

    # With --warn-exit: findings → exit 1
    rc=0
    (cd "$ROOT_DIR" && "$ANALYZER_BIN" --warn-exit --no-db mc_tests/tests/test16_dangerous_functions.c >/dev/null 2>&1) || rc=$?
    expect_eq "$rc" "1" "--warn-exit text mode: exit 1 when findings reported"

    # With --warn-exit: clean file → exit 0
    rc=0
    (cd "$ROOT_DIR" && "$ANALYZER_BIN" --warn-exit --no-db mc_tests/tests/test30_clean_file.c >/dev/null 2>&1) || rc=$?
    expect_eq "$rc" "0" "--warn-exit text mode: exit 0 when no findings"

    # JSON mode without --warn-exit: findings → exit 0
    rc=0
    (cd "$ROOT_DIR" && "$ANALYZER_BIN" --json --no-db mc_tests/tests/test16_dangerous_functions.c >/dev/null 2>&1) || rc=$?
    expect_eq "$rc" "0" "no --warn-exit json mode: exit 0 despite findings"

    # JSON mode with --warn-exit: findings → exit 1, valid JSON
    rc=0
    local json_out
    json_out="$(cd "$ROOT_DIR" && "$ANALYZER_BIN" --json --warn-exit --no-db mc_tests/tests/test16_dangerous_functions.c 2>/dev/null)" || rc=$?
    expect_eq "$rc" "1" "--warn-exit json mode: exit 1 when findings reported"

    if echo "$json_out" | head -1 | grep -q '^{"files":\['; then
        log_pass "--warn-exit json mode: valid JSON output"
        passed=$((passed+1))
    else
        log_fail "--warn-exit json mode: invalid JSON output"
        failed=$((failed+1))
    fi

    # JSON mode with --warn-exit: clean file → exit 0
    rc=0
    (cd "$ROOT_DIR" && "$ANALYZER_BIN" --json --warn-exit --no-db mc_tests/tests/test30_clean_file.c >/dev/null 2>&1) || rc=$?
    expect_eq "$rc" "0" "--warn-exit json mode: exit 0 when no findings"
}

# ----------------------------------------------------------------------
# Test 16b: GCC and SARIF modes exit 1 on findings by default
# ----------------------------------------------------------------------
test_gcc_sarif_default_exit() {
    log_info "=== Test 16b: GCC/SARIF default exit on findings ==="

    # Plain text without --warn-exit: findings → exit 0 (legacy)
    local rc=0
    (cd "$ROOT_DIR" && "$ANALYZER_BIN" --no-db mc_tests/tests/test16_dangerous_functions.c >/dev/null 2>&1) || rc=$?
    expect_eq "$rc" "0" "plain text: exit 0 despite findings (legacy)"

    # JSON without --warn-exit: findings → exit 0 (legacy)
    rc=0
    (cd "$ROOT_DIR" && "$ANALYZER_BIN" --json --no-db mc_tests/tests/test16_dangerous_functions.c >/dev/null 2>&1) || rc=$?
    expect_eq "$rc" "0" "json: exit 0 despite findings (legacy)"

    # GCC mode without --warn-exit: findings → exit 1
    rc=0
    (cd "$ROOT_DIR" && "$ANALYZER_BIN" --gcc --no-db mc_tests/tests/test16_dangerous_functions.c >/dev/null 2>&1) || rc=$?
    expect_eq "$rc" "1" "gcc mode: exit 1 on findings by default"

    # GCC mode: clean file → exit 0
    rc=0
    (cd "$ROOT_DIR" && "$ANALYZER_BIN" --gcc --no-db mc_tests/tests/test30_clean_file.c >/dev/null 2>&1) || rc=$?
    expect_eq "$rc" "0" "gcc mode: exit 0 on clean file"

    # SARIF mode without --warn-exit: findings → exit 1
    rc=0
    (cd "$ROOT_DIR" && "$ANALYZER_BIN" --sarif --no-db mc_tests/tests/test16_dangerous_functions.c >/dev/null 2>&1) || rc=$?
    expect_eq "$rc" "1" "sarif mode: exit 1 on findings by default"

    # SARIF mode: clean file → exit 0
    rc=0
    (cd "$ROOT_DIR" && "$ANALYZER_BIN" --sarif --no-db mc_tests/tests/test30_clean_file.c >/dev/null 2>&1) || rc=$?
    expect_eq "$rc" "0" "sarif mode: exit 0 on clean file"
}

# ----------------------------------------------------------------------
# Test 17: Finding pipeline sees extra-check findings
# ----------------------------------------------------------------------
test_finding_pipeline_extra_checks() {
    log_info "=== Test 17: Finding pipeline – extra-check findings ==="

    # env_usage.c: pipeline must produce insecure_env_usage findings
    local out_env
    out_env="$(cd "$ROOT_DIR" && "$ANALYZER_BIN" --no-db mc_tests/tests/env_usage.c 2>&1)" || true

    local env_count
    env_count="$(echo "$out_env" | grep -c 'insecure_env_usage:' || true)"
    expect_eq "$env_count" "4" "pipeline: env_usage.c produces 4 insecure_env_usage findings"

    # malloc_bad.c: pipeline must produce malloc_size_mismatch findings
    local out_malloc
    out_malloc="$(cd "$ROOT_DIR" && "$ANALYZER_BIN" --no-db mc_tests/tests/malloc_bad.c 2>&1)" || true

    local malloc_count
    malloc_count="$(echo "$out_malloc" | grep -c 'malloc_size_mismatch:' || true)"
    expect_eq "$malloc_count" "2" "pipeline: malloc_bad.c produces 2 malloc_size_mismatch findings"

    # double_close.c: pipeline must produce double_close findings
    local out_dc
    out_dc="$(cd "$ROOT_DIR" && "$ANALYZER_BIN" --no-db mc_tests/tests/double_close.c 2>&1)" || true

    local dc_count
    dc_count="$(echo "$out_dc" | grep -c 'double_close:' || true)"
    expect_eq "$dc_count" "2" "pipeline: double_close.c produces 2 double_close findings"

    # All three fixture types also produce return-value findings via the same pipeline
    local retval_env retval_malloc retval_dc
    retval_env="$(echo "$out_env" | grep -c 'ignored return of' || true)"
    retval_malloc="$(echo "$out_malloc" | grep -c 'stored but unchecked' || true)"
    retval_dc="$(echo "$out_dc" | grep -c 'ignored return of' || true)"

    expect_eq "$retval_env" "1" "pipeline: env_usage.c retval finding coexists"
    expect_eq "$retval_malloc" "4" "pipeline: malloc_bad.c retval findings coexist"
    expect_eq "$retval_dc" "2" "pipeline: double_close.c retval findings coexist"
}

# Test 18: JSON issue_count includes extra-check diagnostics
# ----------------------------------------------------------------------
test_json_issue_count() {
    log_info "=== Test 18: JSON issue_count parity with text mode ==="

    # double_close.c text mode: 2 unchecked-return + 2 double_close = 4
    local json_dc
    json_dc="$(cd "$ROOT_DIR" && "$ANALYZER_BIN" --json --no-db mc_tests/tests/double_close.c 2>/dev/null)" || true

    if echo "$json_dc" | grep -q '"issue_count"'; then
        log_pass "JSON output contains issue_count field"
        passed=$((passed+1))
    else
        log_fail "JSON output missing issue_count field"
        failed=$((failed+1))
    fi

    local ic_dc
    ic_dc="$(echo "$json_dc" | grep -o '"issue_count":[0-9]*' | head -1 | grep -o '[0-9]*$')"
    expect_eq "$ic_dc" "4" "JSON issue_count for double_close.c matches text-mode total"

    # malloc_bad.c text mode: 4 retval + 2 malloc_size_mismatch = 6
    local json_mb
    json_mb="$(cd "$ROOT_DIR" && "$ANALYZER_BIN" --json --no-db mc_tests/tests/malloc_bad.c 2>/dev/null)" || true
    local ic_mb
    ic_mb="$(echo "$json_mb" | grep -o '"issue_count":[0-9]*' | head -1 | grep -o '[0-9]*$')"
    expect_eq "$ic_mb" "6" "JSON issue_count for malloc_bad.c matches text-mode total"
}

# Test 18b: JSON diagnostics array includes extra-check findings
# ----------------------------------------------------------------------
test_json_diagnostics() {
    log_info "=== Test 18b: JSON diagnostics array for extra-check findings ==="

    # env_usage.c: 4 insecure_env_usage findings should appear in diagnostics
    local json_env
    json_env="$(cd "$ROOT_DIR" && "$ANALYZER_BIN" --json --no-db mc_tests/tests/env_usage.c 2>/dev/null)" || true

    if echo "$json_env" | grep -q '"diagnostics"'; then
        log_pass "JSON output contains diagnostics array"
        passed=$((passed+1))
    else
        log_fail "JSON output missing diagnostics array"
        failed=$((failed+1))
    fi

    local diag_env_count
    diag_env_count="$(echo "$json_env" | grep -o '"category":"insecure_env_usage"' | wc -l)"
    expect_eq "$diag_env_count" "4" "JSON diagnostics: 4 insecure_env_usage for env_usage.c"

    # double_close.c: 2 double_close findings in diagnostics
    local json_dc
    json_dc="$(cd "$ROOT_DIR" && "$ANALYZER_BIN" --json --no-db mc_tests/tests/double_close.c 2>/dev/null)" || true

    local diag_dc_count
    diag_dc_count="$(echo "$json_dc" | grep -o '"category":"double_close"' | wc -l)"
    expect_eq "$diag_dc_count" "2" "JSON diagnostics: 2 double_close for double_close.c"

    # malloc_bad.c: 2 malloc_size_mismatch findings in diagnostics
    local json_mb
    json_mb="$(cd "$ROOT_DIR" && "$ANALYZER_BIN" --json --no-db mc_tests/tests/malloc_bad.c 2>/dev/null)" || true

    local diag_mb_count
    diag_mb_count="$(echo "$json_mb" | grep -o '"category":"malloc_size_mismatch"' | wc -l)"
    expect_eq "$diag_mb_count" "2" "JSON diagnostics: 2 malloc_size_mismatch for malloc_bad.c"

    # diagnostics entries have line and category fields
    local diag_line_count
    diag_line_count="$(echo "$json_env" | grep -c '"line":' || true)"
    if [ "$diag_line_count" -gt 0 ]; then
        log_pass "diagnostics entries contain line field"
        passed=$((passed+1))
    else
        log_fail "diagnostics entries missing line field"
        failed=$((failed+1))
    fi
}

# ----------------------------------------------------------------------
# Test 19: DB assertions for non-call findings and suppression effects
# ----------------------------------------------------------------------
test_db_extra_check_facts() {
    log_info "=== Test 19: DB facts for extra-check findings ==="

    local db="${ROOT_DIR}/mc_tests/extra_facts_test.db"
    rm -f "$db"

    # --- env_usage.c: 4 insecure_env_usage + 1 retval_unchecked = 5 ---
    (cd "$ROOT_DIR" && "$ANALYZER_BIN" --db "$db" mc_tests/tests/env_usage.c >/dev/null 2>&1) || true

    local run_id_env
    run_id_env="$(sqlite3 "$db" "SELECT id FROM runs WHERE filename LIKE '%env_usage.c%' LIMIT 1;")"

    local ec_env
    ec_env="$(sqlite3 "$db" "SELECT error_count FROM runs WHERE id=$run_id_env;")"
    expect_eq "$ec_env" "5" "env_usage.c: error_count=5"

    local env_facts
    env_facts="$(sqlite3 "$db" "SELECT COUNT(*) FROM facts WHERE run_id=$run_id_env AND kind='insecure_env_usage';")"
    expect_eq "$env_facts" "4" "env_usage.c: 4 insecure_env_usage facts"

    local env_retval
    env_retval="$(sqlite3 "$db" "SELECT COUNT(*) FROM facts WHERE run_id=$run_id_env AND kind='retval_unchecked';")"
    expect_eq "$env_retval" "1" "env_usage.c: 1 retval_unchecked fact"

    # --- malloc_bad.c: 2 malloc_size_mismatch + 4 retval_unchecked = 6 ---
    (cd "$ROOT_DIR" && "$ANALYZER_BIN" --db "$db" mc_tests/tests/malloc_bad.c >/dev/null 2>&1) || true

    local run_id_mb
    run_id_mb="$(sqlite3 "$db" "SELECT id FROM runs WHERE filename LIKE '%malloc_bad.c%' LIMIT 1;")"

    local ec_mb
    ec_mb="$(sqlite3 "$db" "SELECT error_count FROM runs WHERE id=$run_id_mb;")"
    expect_eq "$ec_mb" "6" "malloc_bad.c: error_count=6"

    local mb_facts
    mb_facts="$(sqlite3 "$db" "SELECT COUNT(*) FROM facts WHERE run_id=$run_id_mb AND kind='malloc_size_mismatch';")"
    expect_eq "$mb_facts" "2" "malloc_bad.c: 2 malloc_size_mismatch facts"

    local mb_retval
    mb_retval="$(sqlite3 "$db" "SELECT COUNT(*) FROM facts WHERE run_id=$run_id_mb AND kind='return_value_check';")"
    expect_eq "$mb_retval" "4" "malloc_bad.c: 4 return_value_check facts"

    # --- double_close.c: 2 double_close + 2 retval_unchecked = 4 ---
    (cd "$ROOT_DIR" && "$ANALYZER_BIN" --db "$db" mc_tests/tests/double_close.c >/dev/null 2>&1) || true

    local run_id_dc
    run_id_dc="$(sqlite3 "$db" "SELECT id FROM runs WHERE filename LIKE '%double_close.c%' LIMIT 1;")"

    local ec_dc
    ec_dc="$(sqlite3 "$db" "SELECT error_count FROM runs WHERE id=$run_id_dc;")"
    expect_eq "$ec_dc" "4" "double_close.c: error_count=4"

    local dc_facts
    dc_facts="$(sqlite3 "$db" "SELECT COUNT(*) FROM facts WHERE run_id=$run_id_dc AND kind='double_close';")"
    expect_eq "$dc_facts" "2" "double_close.c: 2 double_close facts"

    local dc_retval
    dc_retval="$(sqlite3 "$db" "SELECT COUNT(*) FROM facts WHERE run_id=$run_id_dc AND kind='retval_unchecked';")"
    expect_eq "$dc_retval" "2" "double_close.c: 2 retval_unchecked facts"

    # --- Suppression: insecure_env_usage suppressed for env_usage.c ---
    local sdb="${ROOT_DIR}/mc_tests/extra_facts_sup.db"
    rm -f "$sdb"

    (cd "$ROOT_DIR" && "$ANALYZER_BIN" --db "$sdb" --suppressions mc_tests/tests/env_usage.sup mc_tests/tests/env_usage.c >/dev/null 2>&1) || true

    local sid_env
    sid_env="$(sqlite3 "$sdb" "SELECT id FROM runs WHERE filename LIKE '%env_usage.c%' LIMIT 1;")"

    local sec_env
    sec_env="$(sqlite3 "$sdb" "SELECT error_count FROM runs WHERE id=$sid_env;")"
    expect_eq "$sec_env" "1" "suppressed env_usage.c: error_count=1 (only retval)"

    local senv_facts
    senv_facts="$(sqlite3 "$sdb" "SELECT COUNT(*) FROM facts WHERE run_id=$sid_env AND kind='insecure_env_usage';")"
    expect_eq "$senv_facts" "0" "suppressed env_usage.c: 0 insecure_env_usage facts"

    # --- Suppression: malloc_size_mismatch suppressed for malloc_bad.c ---
    (cd "$ROOT_DIR" && "$ANALYZER_BIN" --db "$sdb" --suppressions mc_tests/tests/malloc_bad.sup mc_tests/tests/malloc_bad.c >/dev/null 2>&1) || true

    local sid_mb
    sid_mb="$(sqlite3 "$sdb" "SELECT id FROM runs WHERE filename LIKE '%malloc_bad.c%' LIMIT 1;")"

    local sec_mb
    sec_mb="$(sqlite3 "$sdb" "SELECT error_count FROM runs WHERE id=$sid_mb;")"
    expect_eq "$sec_mb" "4" "suppressed malloc_bad.c: error_count=4 (only retval)"

    local smb_facts
    smb_facts="$(sqlite3 "$sdb" "SELECT COUNT(*) FROM facts WHERE run_id=$sid_mb AND kind='malloc_size_mismatch';")"
    expect_eq "$smb_facts" "0" "suppressed malloc_bad.c: 0 malloc_size_mismatch facts"

    # --- Suppression: double_close suppressed for double_close.c ---
    (cd "$ROOT_DIR" && "$ANALYZER_BIN" --db "$sdb" --suppressions mc_tests/tests/double_close.sup mc_tests/tests/double_close.c >/dev/null 2>&1) || true

    local sid_dc
    sid_dc="$(sqlite3 "$sdb" "SELECT id FROM runs WHERE filename LIKE '%double_close.c%' LIMIT 1;")"

    local sec_dc
    sec_dc="$(sqlite3 "$sdb" "SELECT error_count FROM runs WHERE id=$sid_dc;")"
    expect_eq "$sec_dc" "2" "suppressed double_close.c: error_count=2 (only retval)"

    local sdc_facts
    sdc_facts="$(sqlite3 "$sdb" "SELECT COUNT(*) FROM facts WHERE run_id=$sid_dc AND kind='double_close';")"
    expect_eq "$sdc_facts" "0" "suppressed double_close.c: 0 double_close facts"

    rm -f "$db" "$sdb"
}

# ----------------------------------------------------------------------
# Test 20: mc_preprocess_clang honors compile_cmd preprocessing flags
# ----------------------------------------------------------------------
test_preprocess_compile_cmd_std() {
    log_info "=== Test 20: mc_preprocess_clang compile_cmd flags ==="

    local helper_src="${ROOT_DIR}/mc_tests/helpers/preproc_std_helper.c"
    local helper_bin="${OUT_DIR}/preproc_std_helper"
    local std_fixture="${ROOT_DIR}/mc_tests/fixtures/preproc_std_fixture.c"
    local compdb_fixture="${ROOT_DIR}/mc_tests/fixtures/preproc_compdb_fixture.c"
    local include_dir="${ROOT_DIR}/mc_tests/fixtures/preproc_compdb_include"

    rm -f "$helper_bin"

    run_cmd clang -std=c11 -Wall -Wextra -Wpedantic \
        -I"${ROOT_DIR}/analyzer/src" \
        -o "$helper_bin" \
        "$helper_src" \
        "${ROOT_DIR}/analyzer/src/mc_preproc.c"

    if run_cmd "$helper_bin" "$std_fixture" "$compdb_fixture" "$include_dir"; then
        log_pass "compile_cmd flags provide -std, -D, and -I without shell interpolation"
        passed=$((passed+1))
    else
        log_fail "compile_cmd preprocessing regression"
        failed=$((failed+1))
    fi
}

# ----------------------------------------------------------------------
# Test 21: analyzer CLI compile_commands integration
# ----------------------------------------------------------------------
test_analyzer_compile_commands_integration() {
    log_info "=== Test 21: analyzer --compile-commands integration ==="

    local compdb="${ROOT_DIR}/mc_tests/fixtures/preproc_cli_compdb/compile_commands.json"
    local macro_src="mc_tests/fixtures/preproc_cli_compdb/preproc_cli_compdb_macro.c"
    local std_src="mc_tests/fixtures/preproc_cli_compdb/preproc_cli_compdb_std.c"
    local fallback_src="mc_tests/fixtures/preproc_std_fixture.c"
    local views="${OUT_DIR}/compile_commands_views.jsonl"
    local macro_line std_line fallback_line

    rm -f "$views"

    (
        cd "$ROOT_DIR"
        "$ANALYZER_BIN" --no-db --compile-commands "$compdb" \
            --dump-views "$views" \
            "$macro_src" "$std_src" "$fallback_src" >/dev/null 2>&1
    )

    if [ ! -s "$views" ]; then
        log_fail "compile_commands integration: dump-views output missing"
        failed=$((failed+1))
        return
    fi

    macro_line="$(grep -F 'preproc_cli_compdb_macro.c' "$views" || true)"
    if echo "$macro_line" | grep -Fq 'compile-cmd-macro' &&
       echo "$macro_line" | grep -Fq 'compile-cmd-include'; then
        log_pass "compile_commands arguments array preserves spaced -I and -D"
        passed=$((passed+1))
    else
        log_fail "compile_commands arguments array lost spaced -I or -D semantics"
        failed=$((failed+1))
    fi

    std_line="$(grep -F 'preproc_cli_compdb_std.c' "$views" || true)"
    if echo "$std_line" | grep -Fq 'std-c2x'; then
        log_pass "compile_commands per-file -std affects preprocessing"
        passed=$((passed+1))
    else
        log_fail "compile_commands per-file -std did not affect preprocessing"
        failed=$((failed+1))
    fi

    fallback_line="$(grep -F 'preproc_std_fixture.c' "$views" || true)"
    if echo "$fallback_line" | grep -Fq 'std-c11'; then
        log_pass "compile_commands no-match keeps default preprocessing flags"
        passed=$((passed+1))
    else
        log_fail "compile_commands no-match changed default preprocessing flags"
        failed=$((failed+1))
    fi
}

# ----------------------------------------------------------------------
# Test 22: Bad compile_commands entry fails loudly
# ----------------------------------------------------------------------
test_bad_compdb_fails() {
    log_info "=== Test 22: bad compile_commands entry fails loudly ==="

    local compdb="${ROOT_DIR}/mc_tests/fixtures/bad_compdb/compile_commands.json"
    local src="mc_tests/fixtures/bad_compdb/bad_compdb.c"

    local rc=0 out
    out="$(cd "$ROOT_DIR" && "$ANALYZER_BIN" --no-db --compile-commands "$compdb" "$src" 2>&1)" || rc=$?

    expect_eq "$rc" "1" "bad compdb: exits non-zero"

    if echo "$out" | grep -q 'preprocessing failed.*matched compilation database entry'; then
        log_pass "bad compdb: error message present"
        passed=$((passed+1))
    else
        log_fail "bad compdb: error message present (got: $out)"
        failed=$((failed+1))
    fi
}

# ----------------------------------------------------------------------
# Test 23: CLI error-path integration tests
# ----------------------------------------------------------------------
test_cli_error_paths() {
    log_info "=== Test 22: CLI error-path integration tests ==="

    local rc out
    local src="${ROOT_DIR}/mc_tests/tests/test01_simple_unchecked.c"

    # --db missing argument
    rc=0
    out="$("$ANALYZER_BIN" --db 2>&1)" || rc=$?
    expect_eq "$rc" "1" "cli: --db missing arg exits 1"
    if echo "$out" | grep -q -- '--db requires a path argument'; then
        log_pass "cli: --db missing arg error message"
        passed=$((passed+1))
    else
        log_fail "cli: --db missing arg error message"
        failed=$((failed+1))
    fi

    # --specdb missing argument
    rc=0
    out="$("$ANALYZER_BIN" --specdb 2>&1)" || rc=$?
    expect_eq "$rc" "1" "cli: --specdb missing arg exits 1"
    if echo "$out" | grep -q -- '--specdb requires a path argument'; then
        log_pass "cli: --specdb missing arg error message"
        passed=$((passed+1))
    else
        log_fail "cli: --specdb missing arg error message"
        failed=$((failed+1))
    fi

    # --compile-commands missing argument
    rc=0
    out="$("$ANALYZER_BIN" --compile-commands 2>&1)" || rc=$?
    expect_eq "$rc" "1" "cli: --compile-commands missing arg exits 1"
    if echo "$out" | grep -q -- '--compile-commands requires a path argument'; then
        log_pass "cli: --compile-commands missing arg error message"
        passed=$((passed+1))
    else
        log_fail "cli: --compile-commands missing arg error message"
        failed=$((failed+1))
    fi

    # --dump-views missing argument
    rc=0
    out="$("$ANALYZER_BIN" --dump-views 2>&1)" || rc=$?
    expect_eq "$rc" "1" "cli: --dump-views missing arg exits 1"
    if echo "$out" | grep -q -- '--dump-views requires a path argument'; then
        log_pass "cli: --dump-views missing arg error message"
        passed=$((passed+1))
    else
        log_fail "cli: --dump-views missing arg error message"
        failed=$((failed+1))
    fi

    # --suppressions missing argument
    rc=0
    out="$("$ANALYZER_BIN" --suppressions 2>&1)" || rc=$?
    expect_eq "$rc" "1" "cli: --suppressions missing arg exits 1"
    if echo "$out" | grep -q -- '--suppressions requires a path argument'; then
        log_pass "cli: --suppressions missing arg error message"
        passed=$((passed+1))
    else
        log_fail "cli: --suppressions missing arg error message"
        failed=$((failed+1))
    fi

    # Unknown option
    rc=0
    out="$("$ANALYZER_BIN" --bogus-flag "$src" 2>&1)" || rc=$?
    expect_eq "$rc" "1" "cli: unknown option exits 1"
    if echo "$out" | grep -q 'unknown option: --bogus-flag'; then
        log_pass "cli: unknown option error message"
        passed=$((passed+1))
    else
        log_fail "cli: unknown option error message"
        failed=$((failed+1))
    fi

    # Nonexistent suppressions file
    rc=0
    out="$("$ANALYZER_BIN" --no-db --suppressions /tmp/nonexistent_mancheck_sup_$$.sup "$src" 2>&1)" || rc=$?
    expect_eq "$rc" "1" "cli: nonexistent suppressions file exits 1"
    if echo "$out" | grep -q 'cannot load suppressions file'; then
        log_pass "cli: nonexistent suppressions file error message"
        passed=$((passed+1))
    else
        log_fail "cli: nonexistent suppressions file error message"
        failed=$((failed+1))
    fi

    # Unwritable dump-views target
    rc=0
    out="$("$ANALYZER_BIN" --no-db --dump-views /nonexistent_dir_$$/views.jsonl "$src" 2>&1)" || rc=$?
    expect_eq "$rc" "1" "cli: unwritable dump-views target exits 1"
    if echo "$out" | grep -q 'cannot open .* for writing'; then
        log_pass "cli: unwritable dump-views target error message"
        passed=$((passed+1))
    else
        log_fail "cli: unwritable dump-views target error message"
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
    test_inline_suppress_comment_only_chain
    test_inline_suppress_nolint_next_warn
    test_inline_suppress_same_line_scope
    test_sarif_output
    test_warn_exit
    test_gcc_sarif_default_exit
    test_finding_pipeline_extra_checks
    test_json_issue_count
    test_json_diagnostics
    test_db_extra_check_facts
    test_preprocess_compile_cmd_std
    test_analyzer_compile_commands_integration
    test_bad_compdb_fails
    test_cli_error_paths
    test_gcc_stream_split

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

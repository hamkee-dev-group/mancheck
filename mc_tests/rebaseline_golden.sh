#!/bin/sh
set -eu

# Directory of this script
DIR=$(cd "$(dirname "$0")" && pwd)
ROOT="$DIR/.."
ANALYZER="$ROOT/analyzer/analyzer"

OUT_DB="$ROOT/mc_tests/out/golden_test.db"

if [ ! -x "$ANALYZER" ]; then
    echo "Analyzer not found at $ANALYZER" >&2
    exit 1
fi

mkdir -p "$ROOT/mc_tests/out"
rm -f "$OUT_DB"

for cfile in "$ROOT/mc_tests/tests"/*.c; do
    [ -e "$cfile" ] || continue

    base=$(basename "$cfile")
    name=${base%.c}
    exp="$ROOT/mc_tests/tests/$name.exp"

    echo "Re-generating $exp"

    tmp=$(mktemp)

    (
        cd "$ROOT"
        # Keep the same invocation style as the golden tests
        "./analyzer/analyzer" --db "mc_tests/out/golden_test.db" "mc_tests/tests/$base" >"$tmp" 2>&1 || true
    )

    # Normalize path prefix so expectations use 'tests/...'
    sed 's|mc_tests/tests/|tests/|g' "$tmp" > "$exp"
    rm -f "$tmp"
done

echo "Done. Review changes with: git diff mc_tests/tests/*.exp"

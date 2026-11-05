#!/usr/bin/env bash
# Use compiler-validated positive/negative cases with recorded commands.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
exec python3 "$root/mc_tests/compare_contracts.py"

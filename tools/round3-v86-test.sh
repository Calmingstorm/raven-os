#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
if [[ "${1:-}" == --help ]]; then exec node "$ROOT/tools/round3-v86-test.mjs" --help; fi
TMP="$(mktemp -d /tmp/raven-round3-v86.XXXXXX)"
trap 'if [[ "${ROUND3_KEEP:-0}" == 1 ]]; then echo "Retained $TMP"; else rm -rf "$TMP"; fi' EXIT
# The build helper requires an empty output directory, separate from fixtures.
bash "$ROOT/tools/round3-guest-build.sh" "$TMP/kernels"
python3 "$ROOT/tools/round3-fixtures.py" "$TMP/fixtures"
node "$ROOT/tools/round3-v86-test.mjs" --kernels "$TMP/kernels" --fixtures "$TMP/fixtures" "$@"

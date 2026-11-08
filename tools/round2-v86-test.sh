#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
if [[ "${1:-}" == --help ]]; then exec node "$ROOT/tools/round2-v86-test.mjs" --help; fi
TMP="$(mktemp -d /tmp/raven-round2-v86.XXXXXX)"
trap 'if [[ "${ROUND2_KEEP:-0}" == 1 ]]; then echo "Retained $TMP"; else rm -rf "$TMP"; fi' EXIT
bash "$ROOT/tools/round2-guest-build.sh" "$TMP/kernels"
node "$ROOT/tools/round2-v86-test.mjs" --kernels "$TMP/kernels" "$@"

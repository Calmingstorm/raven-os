#!/usr/bin/env bash
# Optional: build separately, no dependency installs or CI coupling.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
exec node "$ROOT/tools/console-v86-test.mjs" "$@"

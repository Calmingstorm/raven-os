#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
./tools/build.sh
./tools/smoke-qemu.sh
./tools/smoke-qemu-disk.sh
./tools/serial-command-test.sh
if git ls-files >/dev/null 2>&1; then
  git diff --check
fi
echo "checks: ok"

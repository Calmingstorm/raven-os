#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
./tools/build.sh
./tools/memory-regression-test.sh
./tools/memory-qemu-test.sh
./tools/keyboard-test.sh
./tools/serial-editor-test.sh
./tools/vga-test.sh
./tools/console-qemu-test.sh
./tools/smoke-qemu.sh
./tools/smoke-qemu-disk.sh
./tools/persistence-qemu-test.sh
./tools/script-exec-test.sh
./tools/serial-command-test.sh
./tools/fsck-corruption-test.sh
./tools/journal-recovery-test.sh
./tools/usb-iso-qemu-test.sh
if git ls-files >/dev/null 2>&1; then
  git diff --check
fi
echo "checks: ok"

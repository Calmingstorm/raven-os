#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
./tools/build.sh
bash ./tools/runtime-symbol-test.sh
bash ./tools/libc-test.sh
bash ./tools/setjmp-test.sh
bash ./tools/clock-test.sh
bash ./tools/ata-test.sh
bash ./tools/wad-test.sh
bash ./tools/runtime-test.sh
bash ./tools/doom-command-test.sh
bash ./tools/round3-qemu-test.sh
./tools/build-wad.sh
./tools/doom-input-test.sh
./tools/doom-key-loss-test.sh
bash ./tools/doom-audit.sh
./tools/divdi3-test.sh
./tools/doom-host-test.sh
./tools/doomgeneric-diff.sh >/dev/null
python3 ./tools/doom-qemu-test.py
python3 "$ROOT/tests/exception-stubs-test.py"
./tools/memory-regression-test.sh
./tools/memory-qemu-test.sh
./tools/wyrd-test.sh
./tools/heap-test.sh
bash ./tools/round2-paging-host-test.sh
bash ./tools/round2-paging-qemu-test.sh
./tools/round2-qemu-test.sh
./tools/keyboard-test.sh
./tools/serial-editor-test.sh
./tools/vga-test.sh
./tools/console-qemu-test.sh
python3 ./tools/gfx-qemu-test.py
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

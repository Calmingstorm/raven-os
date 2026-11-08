#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="$(mktemp -d /tmp/raven-round2-paging-qemu.XXXXXX)"
trap 'if [[ "${ROUND2_KEEP:-0}" == 1 ]]; then echo "evidence: $OUT"; else rm -rf "$OUT"; fi' EXIT
for mode in 0 1 2 3 4 5 6; do
  bash "$ROOT/tools/round2-paging-build.sh" "$OUT" "$mode"
  dest="$OUT/mode-$mode"
  # Real Multiboot entry, no disk, NIC, image writes or full kernel main.
  code=0
  timeout 10 qemu-system-i386 -machine pc -cpu pentium3 -m 128M \
    -kernel "$dest/round2-paging.kernel" -nodefaults -display none -serial stdio \
    -monitor none -no-reboot -no-shutdown \
    -device isa-debug-exit,iobase=0xf4,iosize=0x04 > "$dest/serial.log" 2>&1 || code=$?
  cat "$dest/serial.log"
  ! grep -q 'ROUND2 FAIL' "$dest/serial.log"
  if [[ "$mode" == 1 || "$mode" == 2 || "$mode" == 3 ]]; then
    [[ "$code" == 124 ]] # fatal production handler halts, timeout expected
    node "$ROOT/tools/round2-paging-validate.mjs" "$mode" "$dest/serial.log"
  else
    [[ "$code" == 33 ]]
    grep -q 'ROUND2 PASS all assertions=' "$dest/serial.log"
    grep -q 'ROUND2 EXIT port=0x00F4 value=0x10' "$dest/serial.log"
  fi
  echo "round2 paging QEMU mode $mode passed (isolated disposable guest)"
done

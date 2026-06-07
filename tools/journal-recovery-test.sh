#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
"$ROOT/tools/build-disk.sh" >/dev/null
LOG1=/tmp/ravenos-journal-recovery-pass1.log
LOG2=/tmp/ravenos-journal-recovery-pass2.log
CLEAN2=/tmp/ravenos-journal-recovery-pass2.clean.log
(
  sleep 2
  printf 'debug-stage-journal /persist/journal-proof recovered-after-reboot\n'
  printf 'fsck\n'
) | timeout 8 qemu-system-i386 \
  -cdrom "$ROOT/build/ravenos.iso" \
  -drive file="$ROOT/build/ravenos-disk.img",format=raw,if=ide,index=0,media=disk \
  -m 128M -display none -serial stdio -monitor none -no-reboot >"$LOG1" 2>&1 || code=$?
code=${code:-0}
if [ "$code" != "0" ] && [ "$code" != "124" ]; then cat "$LOG1"; exit "$code"; fi
(
  sleep 2
  printf 'disk\n'
  printf 'load /persist/journal-proof\n'
  printf 'cat /persist/journal-proof\n'
  printf 'fsck\n'
) | timeout 8 qemu-system-i386 \
  -cdrom "$ROOT/build/ravenos.iso" \
  -drive file="$ROOT/build/ravenos-disk.img",format=raw,if=ide,index=0,media=disk \
  -m 128M -display none -serial stdio -monitor none -no-reboot >"$LOG2" 2>&1 || code=$?
code=${code:-0}
if [ "$code" != "0" ] && [ "$code" != "124" ]; then cat "$LOG2"; exit "$code"; fi
LC_ALL=C tr -cd '\11\12\15\40-\176' < "$LOG2" > "$CLEAN2"
grep -q '/persist/journal-proof = recovered-after-reboot' "$CLEAN2"
grep -q 'loaded /persist/journal-proof into ramfs' "$CLEAN2"
grep -q '/persist/journal-proof: recovered-after-reboot' "$CLEAN2"
grep -q 'ravenfs fsck: files=' "$CLEAN2"
grep -q 'status=clean journal=clean' "$CLEAN2"
echo "journal recovery test: ok"

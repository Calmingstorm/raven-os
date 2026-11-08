#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# A private directory per run: several users run these on one machine, so fixed /tmp names collide.
RAVEN_TMP="$(mktemp -d "${TMPDIR:-/tmp}/ravenos-test.XXXXXX")"
trap 'status=$?; [ "$status" -eq 0 ] && rm -rf "$RAVEN_TMP"; exit "$status"' EXIT
"$ROOT/tools/build-disk.sh" >/dev/null
LOG1=$RAVEN_TMP/ravenos-persistence-pass1.log
LOG2=$RAVEN_TMP/ravenos-persistence-pass2.log
CLEAN1=$RAVEN_TMP/ravenos-persistence-pass1.clean.log
CLEAN2=$RAVEN_TMP/ravenos-persistence-pass2.clean.log
(
  sleep 2
  printf 'disk\n'
  printf 'edit /persist/proof survives-reboot\n'
  printf 'save /persist/proof\n'
  printf 'cat /persist/proof\n'
) | timeout 8 qemu-system-i386 \
  -cdrom "$ROOT/build/ravenos.iso" \
  -drive file="$ROOT/build/ravenos-disk.img",format=raw,if=ide,index=0,media=disk \
  -m 128M -display none -serial stdio -monitor none -no-reboot >"$LOG1" 2>&1 || code=$?
code=${code:-0}
if [ "$code" != "0" ] && [ "$code" != "124" ]; then cat "$LOG1"; exit "$code"; fi
LC_ALL=C tr -cd '\11\12\15\40-\176' < "$LOG1" > "$CLEAN1"
grep -q 'ATA-backed persistent image' "$CLEAN1"
grep -q '/persist/proof: survives-reboot' "$CLEAN1"
(
  sleep 2
  printf 'disk\n'
  printf 'load /persist/proof\n'
  printf 'cat /persist/proof\n'
) | timeout 8 qemu-system-i386 \
  -cdrom "$ROOT/build/ravenos.iso" \
  -drive file="$ROOT/build/ravenos-disk.img",format=raw,if=ide,index=0,media=disk \
  -m 128M -display none -serial stdio -monitor none -no-reboot >"$LOG2" 2>&1 || code=$?
code=${code:-0}
if [ "$code" != "0" ] && [ "$code" != "124" ]; then cat "$LOG2"; exit "$code"; fi
LC_ALL=C tr -cd '\11\12\15\40-\176' < "$LOG2" > "$CLEAN2"
grep -q '/persist/proof = survives-reboot' "$CLEAN2"
grep -q 'loaded /persist/proof into ramfs' "$CLEAN2"
grep -q '/persist/proof: survives-reboot' "$CLEAN2"
echo "persistence qemu test: ok"

#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
"$ROOT/tools/build-disk.sh" >/dev/null
LOG=/tmp/ravenos-serial-command-test.log
(
  sleep 2
  printf 'serial\n'
  printf 'rings\n'
  printf 'ata\n'
  printf 'procps\n'
  printf 'edit /home/serial validated\n'
  printf 'cat /home/serial\n'
  printf 'save /home/serial\n'
  printf 'rm /home/serial\n'
  printf 'load /home/serial\n'
  printf 'cat /home/serial\n'
  printf 'run netdemo\n'
  printf 'metrics\n'
) | timeout 12 qemu-system-i386 \
  -cdrom "$ROOT/build/ravenos.iso" \
  -drive file="$ROOT/build/ravenos-disk.img",format=raw,if=ide,index=0,media=disk \
  -m 128M -display none -serial stdio -monitor none -no-reboot >"$LOG" 2>&1 || code=$?
code=${code:-0}
if [ "$code" != "0" ] && [ "$code" != "124" ]; then
  cat "$LOG"
  exit "$code"
fi
CLEAN=/tmp/ravenos-serial-command-test.clean.log
LC_ALL=C tr -cd '\11\12\15\40-\176' < "$LOG" > "$CLEAN"
grep -q 'serial console' "$CLEAN"
grep -q 'rings / descriptor tables' "$CLEAN"
grep -q 'ata probe' "$CLEAN"
grep -q 'processes' "$CLEAN"
grep -q '/home/serial: validated' "$CLEAN"
grep -q 'user.netdemo: sent loopback payload' "$CLEAN"
grep -q 'raven_task_switches' "$CLEAN"
echo "serial command test: ok"

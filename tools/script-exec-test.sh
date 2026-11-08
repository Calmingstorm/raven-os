#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# A private directory per run: several users run these on one machine, so fixed /tmp names collide.
RAVEN_TMP="$(mktemp -d "${TMPDIR:-/tmp}/ravenos-test.XXXXXX")"
trap 'status=$?; [ "$status" -eq 0 ] && rm -rf "$RAVEN_TMP"; exit "$status"' EXIT
"$ROOT/tools/build-disk.sh" >/dev/null
LOG=$RAVEN_TMP/ravenos-script-exec-test.log
CLEAN=$RAVEN_TMP/ravenos-script-exec-test.clean.log
(
  sleep 2
  printf 'edit /bin/note.rvn RVNEXE write /home/script-note script-exec-ok\n'
  printf 'save /bin/note.rvn\n'
  printf 'load /bin/note.rvn\n'
  printf 'run /bin/note.rvn\n'
  printf 'cat /home/script-note\n'
  printf 'edit /bin/talker.rvn RVNEXE send script-packet\n'
  printf 'run /bin/talker.rvn\n'
  printf 'net\n'
) | timeout 10 qemu-system-i386 \
  -cdrom "$ROOT/build/ravenos.iso" \
  -drive file="$ROOT/build/ravenos-disk.img",format=raw,if=ide,index=0,media=disk \
  -m 128M -display none -serial stdio -monitor none -no-reboot >"$LOG" 2>&1 || code=$?
code=${code:-0}
if [ "$code" != "0" ] && [ "$code" != "124" ]; then cat "$LOG"; exit "$code"; fi
LC_ALL=C tr -cd '\11\12\15\40-\176' < "$LOG" > "$CLEAN"
grep -q 'user.note: wrote /home/script-note' "$CLEAN"
grep -q '/home/script-note: script-exec-ok' "$CLEAN"
grep -q 'user.talker: sent payload' "$CLEAN"
grep -q 'last=script-packet' "$CLEAN"
echo "script exec test: ok"

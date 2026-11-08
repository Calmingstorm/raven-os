#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# A private directory per run: several users run these on one machine, so fixed /tmp names collide.
RAVEN_TMP="$(mktemp -d "${TMPDIR:-/tmp}/ravenos-test.XXXXXX")"
trap 'status=$?; [ "$status" -eq 0 ] && rm -rf "$RAVEN_TMP"; exit "$status"' EXIT
"$ROOT/tools/build-disk.sh" >/dev/null
if timeout 8 qemu-system-i386 \
  -cdrom "$ROOT/build/ravenos.iso" \
  -drive file="$ROOT/build/ravenos-disk.img",format=raw,if=ide,index=0,media=disk \
  -m 128M -display none -serial mon:stdio -no-reboot >$RAVEN_TMP/ravenos-qemu-disk-smoke.log 2>&1; then
  true
else
  code=$?
  if [ "$code" != "124" ]; then
    cat $RAVEN_TMP/ravenos-qemu-disk-smoke.log
    exit "$code"
  fi
fi
echo "qemu disk smoke: ok (8s no crash)"

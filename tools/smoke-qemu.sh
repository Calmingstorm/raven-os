#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
"$ROOT/tools/build.sh" >/dev/null
if timeout 8 qemu-system-i386 -cdrom "$ROOT/build/ravenos.iso" -m 128M -display none -serial mon:stdio -no-reboot >/tmp/ravenos-qemu-smoke.log 2>&1; then
  true
else
  code=$?
  if [ "$code" != "124" ]; then
    cat /tmp/ravenos-qemu-smoke.log
    exit "$code"
  fi
fi
echo "qemu boot smoke: ok (8s no crash)"

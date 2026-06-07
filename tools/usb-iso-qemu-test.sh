#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ISO="$ROOT/build/ravenos-usb.iso"
"$ROOT/tools/build-usb-iso.sh" >/dev/null
OUT="$ROOT/build/usb-iso-qemu.log"
rm -f "$OUT"
# Boot the USB ISO as a raw IDE disk, not as a CD-ROM, to catch non-hybrid images.
rc=0
timeout 12 qemu-system-i386 \
  -m 128M \
  -drive file="$ISO",format=raw,if=ide \
  -serial stdio \
  -display none \
  -no-reboot >"$OUT" 2>&1 || rc=$?
if [[ "$rc" != "0" && "$rc" != "124" ]]; then
  cat "$OUT" >&2
  exit "$rc"
fi
if ! grep -q 'RavenOS' "$OUT"; then
  cat "$OUT" >&2
  echo "error: RavenOS banner not observed while booting USB ISO as disk" >&2
  exit 1
fi
if ! grep -q 'raven>' "$OUT"; then
  cat "$OUT" >&2
  echo "error: shell prompt not observed while booting USB ISO as disk" >&2
  exit 1
fi
echo "usb iso qemu boot: ok"

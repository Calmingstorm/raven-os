#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="$ROOT/build"
BASE_ISO="$BUILD/ravenos.iso"
USB_ISO="$BUILD/ravenos-usb.iso"
"$ROOT/tools/build.sh" >/dev/null
cp "$BASE_ISO" "$USB_ISO"
if command -v isohybrid >/dev/null 2>&1; then
  # Make/confirm the image is suitable for writing directly to USB media.
  isohybrid "$USB_ISO" 2>/dev/null || true
fi
if ! file "$USB_ISO" | grep -qi 'bootable'; then
  echo "error: $USB_ISO does not appear bootable according to file(1)" >&2
  file "$USB_ISO" >&2
  exit 1
fi
if command -v grub-file >/dev/null 2>&1; then
  grub-file --is-x86-multiboot "$BUILD/ravenos.kernel"
fi
sha256sum "$USB_ISO" > "$USB_ISO.sha256"
ls -lh "$USB_ISO"
cat "$USB_ISO.sha256"
echo "usb-iso: $USB_ISO"
echo "write to removable media with a raw image writer such as balenaEtcher, Raspberry Pi Imager, GNOME Disks, or Rufus raw/image mode"

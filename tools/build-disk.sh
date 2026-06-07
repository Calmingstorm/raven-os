#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="$ROOT/build"
DISK="$BUILD/ravenos-disk.img"
"$ROOT/tools/build.sh" >/dev/null
rm -f "$DISK"
qemu-img create -f raw "$DISK" 64M >/dev/null
# No host filesystem formatting, no loop mounts, no grub-install to a real device.
# RavenOS can boot as a kernel payload from ISO today; this raw image is the
# dev target used by the ATA PIO read/write path and future installed boot.
printf 'RAVENDISK0 dev target for RavenOS ATA/RavenFS persistence\n' | dd of="$DISK" bs=512 seek=20 conv=notrunc status=none
echo "disk: $DISK"

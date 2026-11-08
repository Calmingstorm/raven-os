#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# A private directory per run: several users run these on one machine, so fixed /tmp names collide.
RAVEN_TMP="$(mktemp -d "${TMPDIR:-/tmp}/ravenos-test.XXXXXX")"
trap 'status=$?; [ "$status" -eq 0 ] && rm -rf "$RAVEN_TMP"; exit "$status"' EXIT
"$ROOT/tools/build-disk.sh" >/dev/null
LOG1=$RAVEN_TMP/ravenos-fsck-corrupt-pass1.log
LOG2=$RAVEN_TMP/ravenos-fsck-corrupt-pass2.log
CLEAN2=$RAVEN_TMP/ravenos-fsck-corrupt-pass2.clean.log
(
  sleep 2
  printf 'edit /persist/fsck-proof clean-before-corrupt\n'
  printf 'save /persist/fsck-proof\n'
  printf 'fsck\n'
) | timeout 8 qemu-system-i386 \
  -cdrom "$ROOT/build/ravenos.iso" \
  -drive file="$ROOT/build/ravenos-disk.img",format=raw,if=ide,index=0,media=disk \
  -m 128M -display none -serial stdio -monitor none -no-reboot >"$LOG1" 2>&1 || code=$?
code=${code:-0}
if [ "$code" != "0" ] && [ "$code" != "124" ]; then cat "$LOG1"; exit "$code"; fi
python3 - <<'PY'
from pathlib import Path
img = Path('build/ravenos-disk.img')
data = bytearray(img.read_bytes())
sector_size = 512
# RavenFS lives at ATA LBA 32; file slot 0 is LBA 33.
slot0 = (32 + 1) * sector_size
checksum_offset = slot0 + 1 + 48 + 320
if slot0 + sector_size > len(data):
    raise SystemExit('image too small')
# Flip the persisted checksum bytes, leaving content intact.
data[checksum_offset] ^= 0x5A
data[checksum_offset + 1] ^= 0xA5
img.write_bytes(data)
PY
(
  sleep 2
  printf 'fsck\n'
) | timeout 8 qemu-system-i386 \
  -cdrom "$ROOT/build/ravenos.iso" \
  -drive file="$ROOT/build/ravenos-disk.img",format=raw,if=ide,index=0,media=disk \
  -m 128M -display none -serial stdio -monitor none -no-reboot >"$LOG2" 2>&1 || code=$?
code=${code:-0}
if [ "$code" != "0" ] && [ "$code" != "124" ]; then cat "$LOG2"; exit "$code"; fi
LC_ALL=C tr -cd '\11\12\15\40-\176' < "$LOG2" > "$CLEAN2"
grep -q 'ravenfs fsck: files=' "$CLEAN2"
grep -q 'errors=1' "$CLEAN2"
grep -q 'status=fail' "$CLEAN2"
echo "fsck corruption test: ok"

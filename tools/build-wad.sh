#!/usr/bin/env bash
# Build the Doom data disk: Freedoom Phase 1 (BSD-3-Clause) as a raw image for the primary IDE slave, padded with
# zeros to a whole number of 256 KiB chunks. The page fetches the disk in chunks of exactly that size, and v86
# mishandles a final chunk that runs past the end of the file. The pinned release is downloaded once into
# build/cache/ and checked against Freedoom's published SHA-256 before anything is extracted.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CACHE="$ROOT/build/cache"
OUT="$ROOT/build/doom"

FREEDOOM_VERSION=0.13.0
ZIP_URL="https://github.com/freedoom/freedoom/releases/download/v$FREEDOOM_VERSION/freedoom-$FREEDOOM_VERSION.zip"
ZIP_SHA256=3f9b264f3e3ce503b4fb7f6bdcb1f419d93c7b546f4df3e874dd878db9688f59   # freedoom-0.13.0-CHECKSUM
WAD_SHA256=7323bcc168c5a45ff10749b339960e98314740a734c30d4b9f3337001f9e703d
WAD_BYTES=28795076
CHUNK_BYTES=262144
IMAGE_BYTES=$(( (WAD_BYTES + CHUNK_BYTES - 1) / CHUNK_BYTES * CHUNK_BYTES ))

zip="$CACHE/freedoom-$FREEDOOM_VERSION.zip"
if ! { [ -f "$zip" ] && echo "$ZIP_SHA256  $zip" | sha256sum -c --status; }; then
  mkdir -p "$CACHE"
  curl -fsSL --retry 3 -o "$zip.part" "$ZIP_URL"
  if ! echo "$ZIP_SHA256  $zip.part" | sha256sum -c --status; then
    rm -f "$zip.part"
    echo "build-wad: freedoom-$FREEDOOM_VERSION.zip does not match its published SHA-256" >&2
    exit 1
  fi
  mv "$zip.part" "$zip"
fi

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
dir="freedoom-$FREEDOOM_VERSION"
unzip -q -d "$tmp" "$zip" "$dir/freedoom1.wad" "$dir/COPYING.txt" "$dir/CREDITS.txt" "$dir/CREDITS-MUSIC.txt"
wad="$tmp/$dir/freedoom1.wad"
if [ "$(stat -c %s "$wad")" != "$WAD_BYTES" ] || ! echo "$WAD_SHA256  $wad" | sha256sum -c --status; then
  echo "build-wad: freedoom1.wad is not the pinned $WAD_BYTES-byte file" >&2
  exit 1
fi

rm -rf "$OUT"
mkdir -p "$OUT/licenses/freedoom"
cp "$wad" "$OUT/freedoom1.img"
truncate -s "$IMAGE_BYTES" "$OUT/freedoom1.img"
for f in COPYING.txt CREDITS.txt CREDITS-MUSIC.txt; do cp "$tmp/$dir/$f" "$OUT/licenses/freedoom/$f"; done
image_sha256="$(sha256sum "$OUT/freedoom1.img" | cut -d' ' -f1)"
cat > "$OUT/disk.json" <<EOF
{
  "wad": {"name": "freedoom1.wad", "version": "$FREEDOOM_VERSION", "bytes": $WAD_BYTES, "sha256": "$WAD_SHA256"},
  "image": {"bytes": $IMAGE_BYTES, "chunk_bytes": $CHUNK_BYTES, "sha256": "$image_sha256"}
}
EOF
echo "doom disk: $OUT/freedoom1.img ($IMAGE_BYTES bytes, sha256 $image_sha256)"

#!/usr/bin/env bash
# Assemble web/dist: the browser page, the v86 emulator, the BIOS files and the kernel.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WEB="$ROOT/web"
DIST="$WEB/dist"

"$ROOT/tools/build.sh" >/dev/null
if [ ! -d "$WEB/node_modules/v86" ]; then
  (cd "$WEB" && npm ci --no-audit --no-fund)
fi

rm -rf "$DIST"
mkdir -p "$DIST/licenses"
cp "$WEB/index.html" "$WEB/style.css" "$WEB/app.js" "$WEB/favicon.svg" "$DIST/"
cp "$WEB/node_modules/v86/build/libv86.mjs" "$WEB/node_modules/v86/build/v86.wasm" "$DIST/"
cp "$WEB/vendor/bios/seabios.bin" "$WEB/vendor/bios/vgabios.bin" "$DIST/"
cp "$ROOT/build/ravenos.kernel" "$DIST/"
cp "$WEB/licenses/"* "$DIST/licenses/"

# Pre-compressed copies, served by nginx's gzip_static to browsers that accept gzip.
find "$DIST" -type f ! -name '*.gz' -exec gzip -9 -k -n {} +
echo "web: $DIST ($(du -sh --exclude='*.gz' "$DIST" | cut -f1) uncompressed)"

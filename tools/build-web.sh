#!/usr/bin/env bash
# Assemble web/dist: the browser page, the v86 emulator, the BIOS files and their source, the kernel,
# and build.json recording exactly what was built.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WEB="$ROOT/web"
DIST="$WEB/dist"

"$ROOT/tools/build.sh" >/dev/null
# Always install from the lockfile, so a stale node_modules can't slip into a build.
(cd "$WEB" && npm ci --no-audit --no-fund --silent)

rm -rf "$DIST"
mkdir -p "$DIST/licenses" "$DIST/source/bios"
cp "$WEB/index.html" "$WEB/style.css" "$WEB/app.js" "$WEB/favicon.svg" "$DIST/"
cp "$WEB/node_modules/v86/build/libv86.mjs" "$WEB/node_modules/v86/build/v86.wasm" "$DIST/"
cp "$WEB/vendor/bios/seabios.bin" "$WEB/vendor/bios/vgabios.bin" "$DIST/"
cp "$ROOT/build/ravenos.kernel" "$DIST/"
cp "$WEB/licenses/"* "$DIST/licenses/"
cp "$WEB/vendor/bios/source/"* "$DIST/source/bios/"

# The BIOS source directory needs an index page: nginx doesn't list directories.
{
  echo '<!doctype html><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">'
  echo '<title>SeaBIOS source for RavenOS</title><link rel="stylesheet" href="../../style.css">'
  echo '<main class="source-index"><h1>SeaBIOS source</h1><ul>'
  for file in "$DIST/source/bios/"*; do
    name="$(basename "$file")"
    [ "$name" = index.html ] || echo "<li><a href=\"$name\">$name</a></li>"
  done
  echo '</ul><pre>'
  sed -e 's/&/\&amp;/g' -e 's/</\&lt;/g' -e 's/>/\&gt;/g' "$WEB/vendor/bios/source/README.md"
  echo '</pre></main>'
} > "$DIST/source/bios/index.html"

# Provenance: deploy-web.sh names the release from this, and the live site serves it.
git_() { git -c safe.directory="$ROOT" -C "$ROOT" "$@"; }
commit="$(git_ rev-parse HEAD)"
dirty=false
[ -z "$(git_ status --porcelain)" ] || dirty=true
built_at="$(date -u +%Y%m%dT%H%M%SZ)"
release="$built_at-${commit:0:7}"
[ "$dirty" = false ] || release="$release-dirty"
(cd "$DIST" && find . -type f ! -name build.json -print0 | sort -z | xargs -0 sha256sum) |
  node -e '
    const [release, commit, dirty, builtAt, v86] = process.argv.slice(1);
    let text = "";
    process.stdin.on("data", chunk => { text += chunk; }).on("end", () => {
      const files = Object.fromEntries(text.trim().split("\n").map(line => {
        const [hash, name] = line.split(/\s+\.\//);
        return [name, hash];
      }));
      const build = { release, commit, dirty: dirty === "true", built_at: builtAt, v86, files };
      process.stdout.write(JSON.stringify(build, null, 1) + "\n");
    });' "$release" "$commit" "$dirty" "$built_at" "$(node -p "require('$WEB/node_modules/v86/package.json').version")" \
  > "$DIST/build.json"

# Pre-compressed copies, served by nginx's gzip_static to browsers that accept gzip.
find "$DIST" -type f ! -name '*.gz' -exec gzip -9 -k -n {} +
echo "web: $DIST, release $release ($(du -sh --exclude='*.gz' "$DIST" | cut -f1) uncompressed)"

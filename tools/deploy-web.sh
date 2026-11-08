#!/usr/bin/env bash
# Publish web/dist to server-2, where nginx serves it to .13's Apache as ravenos.calmingstorm.net.
#
# 1. web/dist must match its build.json (every hash, every .gz copy, the required files).
# 2. A new release uploads into a staging directory, renamed into releases/ when complete. An existing
#    release is never written to: if it holds the same build it is reused, otherwise the deploy stops.
# 3. `current` switches atomically. The live site must then serve this build.json and these exact files
#    and pass tools/check-web.mjs, or `current` goes back to what it was (or away, if there was none).
# 4. Old releases are pruned only after a deploy passes, never the live one.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DIST="$ROOT/web/dist"
HOST="${RAVENOS_WEB_HOST:-root@192.168.1.41}"
SITE="${RAVENOS_SITE:-https://ravenos.calmingstorm.net/}"
BASE="${RAVENOS_WEB_BASE:-/srv/ravenos}"
KEEP=5
REQUIRED=(index.html app.js style.css favicon.svg libv86.mjs v86.wasm seabios.bin vgabios.bin ravenos.kernel
          licenses/v86-LICENSE.txt licenses/LGPL-3.0.txt licenses/GPL-3.0.txt licenses/GPL-2.0.txt
          licenses/freedoom-COPYING.txt licenses/freedoom-CREDITS.txt licenses/freedoom-CREDITS-MUSIC.txt
          licenses/GCC-RUNTIME-LIBRARY-EXCEPTION-3.1.txt source/ravenos-source.tar.gz source/libgcc-divdi3.txt
          source/bios/index.html source/bios/README.md source/bios/seabios-1.16.2.tar.gz
          source/bios/seabios.config source/bios/fetch-and-build-seabios.sh source/bios/COPYING.LESSER)

[ -s "$DIST/build.json" ] || { echo "error: no $DIST/build.json; run tools/build-web.sh" >&2; exit 1; }
node "$ROOT/tools/verify-web.mjs" dist "$DIST" "${REQUIRED[@]}"
release="$(node -e 'process.stdout.write(JSON.parse(require("fs").readFileSync(process.argv[1], "utf8")).release)' "$DIST/build.json")"
[[ "$release" =~ ^[0-9]{8}T[0-9]{6}Z-[0-9a-f]{7}(-dirty)?$ ]] || { echo "error: bad release name '$release'" >&2; exit 1; }

previous="$(ssh "$HOST" "readlink '$BASE/current' || true")"
if ssh "$HOST" "test -e '$BASE/releases/$release'"; then
  if ssh "$HOST" "cat '$BASE/releases/$release/build.json'" | cmp -s - "$DIST/build.json"; then
    echo "releases/$release is already on server-2 with this build; reusing it"
  else
    echo "error: releases/$release exists on server-2 with different content; not touching it" >&2
    exit 1
  fi
else
  staging="releases/.incoming-$release-$$"
  ssh "$HOST" "mkdir -p '$BASE/$staging'"
  rsync -rlt --delete --chmod=D755,F644 "$DIST/" "$HOST:$BASE/$staging/"
  ssh "$HOST" "mv -T '$BASE/$staging' '$BASE/releases/$release'"
fi

switch_to() { ssh "$HOST" "cd '$BASE' && ln -sfn '$1' current.new && mv -T current.new current"; }
restore() {
  if [ -n "$previous" ]; then switch_to "$previous"; else ssh "$HOST" "rm -f '$BASE/current'"; fi
}
switch_to "releases/$release"
if ! node "$ROOT/tools/verify-web.mjs" site "$SITE" "$DIST" || ! node "$ROOT/tools/check-web.mjs" --url "$SITE"; then
  echo "error: $SITE failed verification; restoring '${previous:-no current release}'" >&2
  restore
  exit 1
fi

ssh "$HOST" "cd '$BASE' && live=\$(readlink current) && ls -1d releases/*/ | sort -r | tail -n +$((KEEP + 1)) |
  while read -r old; do [ \"\${old%/}\" = \"\$live\" ] || rm -r -- \"\$old\"; done"
echo "live: $release (verified at $SITE)"

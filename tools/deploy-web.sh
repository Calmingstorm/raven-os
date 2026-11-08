#!/usr/bin/env bash
# Publish web/dist to server-2, where nginx serves it to .13's Apache as ravenos.calmingstorm.net.
# Each deploy is a new release directory. The `current` symlink switches atomically, then the live site
# must serve this build's build.json and pass tools/check-web.mjs; otherwise it switches back.
# Old releases are pruned only after a deploy passes.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DIST="$ROOT/web/dist"
HOST="${RAVENOS_WEB_HOST:-root@192.168.1.41}"
SITE="${RAVENOS_SITE:-https://ravenos.calmingstorm.net/}"
BASE=/srv/ravenos
KEEP=5
REQUIRED=(index.html app.js style.css favicon.svg libv86.mjs v86.wasm seabios.bin vgabios.bin ravenos.kernel
          build.json source/bios/index.html source/bios/seabios-1.16.2.tar.gz)

for file in "${REQUIRED[@]}"; do
  [ -s "$DIST/$file" ] || { echo "error: $DIST/$file is missing or empty; run tools/build-web.sh" >&2; exit 1; }
done
release_of() { node -e 'process.stdout.write(JSON.parse(require("fs").readFileSync(0, "utf8")).release || "")'; }
release="$(release_of < "$DIST/build.json")"
[[ "$release" =~ ^[0-9]{8}T[0-9]{6}Z-[0-9a-f]{7}(-dirty)?$ ]] || { echo "error: bad release name '$release' in build.json" >&2; exit 1; }

previous="$(ssh "$HOST" "readlink '$BASE/current' || true")"
ssh "$HOST" "mkdir -p '$BASE/releases/$release'"
rsync -rlt --delete --chmod=D755,F644 "$DIST/" "$HOST:$BASE/releases/$release/"
switch_to() { ssh "$HOST" "cd '$BASE' && ln -sfn '$1' current.new && mv -T current.new current"; }
switch_to "releases/$release"

served="$(curl -fsS --max-time 20 "${SITE}build.json" | release_of)" || served=""
if [ "$served" != "$release" ] || ! node "$ROOT/tools/check-web.mjs" --url "$SITE"; then
  echo "error: $SITE failed verification (serving '${served:-nothing}'); switching back to '${previous:-nothing}'" >&2
  if [ -n "$previous" ]; then switch_to "$previous"; fi
  exit 1
fi

ssh "$HOST" "cd '$BASE' && live=\$(readlink current) && ls -1d releases/*/ | sort -r | tail -n +$((KEEP + 1)) |
  while read -r old; do [ \"\${old%/}\" = \"\$live\" ] || rm -r -- \"\$old\"; done"
echo "live: $release (verified at $SITE)"

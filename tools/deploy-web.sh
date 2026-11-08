#!/usr/bin/env bash
# Publish web/dist to server-2, where nginx serves it to .13's Apache as ravenos.calmingstorm.net.
# Each deploy is a new release directory; the `current` symlink switches atomically.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DIST="$ROOT/web/dist"
HOST="${RAVENOS_WEB_HOST:-root@192.168.1.41}"
BASE=/srv/ravenos
KEEP=5

[ -f "$DIST/index.html" ] || { echo "error: no $DIST/index.html; run tools/build-web.sh first" >&2; exit 1; }
# safe.directory: the deploy runs as whoever holds the SSH key, not always the checkout's owner.
git_() { git -c safe.directory="$ROOT" -C "$ROOT" "$@"; }
release="$(date -u +%Y%m%dT%H%M%SZ)-$(git_ rev-parse --short HEAD)"
git_ diff --quiet HEAD -- || release="$release-dirty"

ssh "$HOST" "mkdir -p '$BASE/releases/$release'"
rsync -rlt --delete --chmod=D755,F644 "$DIST/" "$HOST:$BASE/releases/$release/"
ssh "$HOST" "set -e
  cd '$BASE'
  ln -sfn 'releases/$release' current.new
  mv -T current.new current
  ls -1d releases/*/ | sort -r | tail -n +$((KEEP + 1)) | while read -r old; do rm -r -- \"\$old\"; done"
echo "live: $release"

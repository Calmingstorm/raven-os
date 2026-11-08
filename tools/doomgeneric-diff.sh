#!/usr/bin/env bash
# Show how third_party/doomgeneric differs from the pinned upstream commit: the retained upstream files with the
# RavenOS changes, which must be exactly the files README.ravenos.md lists. Exits 1 if any other upstream file
# differs or a file is neither upstream's nor RavenOS's. Fetches the pinned commit once into build/cache/ with git,
# whose object hashes vouch for the contents.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
COMMIT=e257a09527062bfb986b6e5e0780839b6cbd5b71
VENDOR="$ROOT/third_party/doomgeneric"
CHANGED=(g_game.c i_input.c i_system.c m_menu.c p_setup.c r_main.c v_video.c)
OURS=(COPYING README.upstream.md README.ravenos.md doomgeneric_ravenos.c raven_doom_input.c raven_doom_input.h)

repo="$ROOT/build/cache/doomgeneric.git"
if ! git -C "$repo" cat-file -e "$COMMIT^{commit}" 2>/dev/null; then
  git init -q --bare "$repo"
  git -C "$repo" fetch -q --depth 1 https://github.com/ozkl/doomgeneric.git "$COMMIT"
fi
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
git -C "$repo" archive "$COMMIT" | tar -x -C "$tmp"
upstream="$tmp/doomgeneric"

status=0
for file in "$VENDOR"/*; do
  name="$(basename "$file")"
  printf '%s\n' "${OURS[@]}" | grep -qx "$name" && continue
  if [ ! -f "$upstream/$name" ]; then
    echo "not from upstream and not listed as RavenOS's: $name" >&2; status=1; continue
  fi
  if printf '%s\n' "${CHANGED[@]}" | grep -qx "$name"; then
    diff -u --label "upstream/$name" --label "ravenos/$name" "$upstream/$name" "$file" || true
  elif ! cmp -s "$upstream/$name" "$file"; then
    echo "changed but not listed in README.ravenos.md: $name" >&2; status=1
  fi
done
for name in "${CHANGED[@]}"; do
  cmp -s "$upstream/$name" "$VENDOR/$name" && { echo "listed as changed but identical to upstream: $name" >&2; status=1; }
done
cmp -s "$tmp/LICENSE" "$VENDOR/COPYING" || { echo "COPYING is not upstream's LICENSE" >&2; status=1; }
cmp -s "$tmp/README.md" "$VENDOR/README.upstream.md" || { echo "README.upstream.md is not upstream's README.md" >&2; status=1; }
exit $status

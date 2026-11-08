#!/usr/bin/env bash
# Build the Doom port as a 32-bit Linux program (tests/doom-host-test.c) and play it on the real data disk.
# Usage: tools/doom-host-test.sh [screenshot dir]
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
"$ROOT/tools/build-wad.sh" >/dev/null
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

# The logical WAD, as RavenOS's /wad/freedoom1.wad presents it: the disk without its padding.
wad_bytes="$(node -p "require('$ROOT/build/doom/disk.json').wad.bytes")"
head -c "$wad_bytes" "$ROOT/build/doom/freedoom1.img" > "$tmp/freedoom1.wad"
printf 'XWAD' | cat - <(tail -c +5 "$tmp/freedoom1.wad" | head -c 65536) > "$tmp/bad.wad"

# _FORTIFY_SOURCE would route fread to __fread_chk, past the test's file wrappers.
common=(-m32 -std=gnu17 -O1 -g -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 -DRAVENOS -I"$ROOT/third_party/doomgeneric" -I"$ROOT/kernel")
objs=()
for src in "$ROOT"/third_party/doomgeneric/*.c; do
  obj="$tmp/$(basename "${src%.c}").o"
  "${CC:-cc}" "${common[@]}" -w -c "$src" -o "$obj"
  objs+=("$obj")
done
"${CC:-cc}" "${common[@]}" -Wall -Wextra -Werror -Wno-unused-parameter -c "$ROOT/tests/doom-host-test.c" -o "$tmp/test.o"
"${CC:-cc}" -m32 "${objs[@]}" "$tmp/test.o" -Wl,--wrap=exit,--wrap=abort,--wrap=fopen,--wrap=fread -o "$tmp/doom-host-test"

# Doom's startup log goes to stdout; keep it out of the way unless a run fails.
for mode in play nographics badwad beginfails errorafter; do
  wad="$tmp/freedoom1.wad"
  [ "$mode" != badwad ] || wad="$tmp/bad.wad"
  args=("$mode" "$wad")
  [ "$mode" != play ] || [ -z "${1:-}" ] || args+=("$1")
  if ! (cd "$tmp" && HOME="$tmp" ./doom-host-test "${args[@]}" > "$tmp/$mode.log" 2> "$tmp/$mode.err"); then
    tail -n 15 "$tmp/$mode.log"
    cat "$tmp/$mode.err" >&2
    echo "doom-host-test: $mode failed" >&2
    exit 1
  fi
  grep '^doom-host-test\|^  disk:' "$tmp/$mode.log"
done

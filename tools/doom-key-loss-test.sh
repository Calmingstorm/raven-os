#!/usr/bin/env bash
# The Doom port's input path through the real keyboard driver, with transitions dropped (tests/doom-key-loss-test.c).
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
"${CC:-cc}" -m32 -std=gnu11 -Wall -Wextra -Werror -O2 -fno-builtin -DKEYBOARD_TEST \
  -I"$ROOT/kernel" -I"$ROOT/third_party/doomgeneric" \
  "$ROOT/kernel/keyboard.c" "$ROOT/kernel/doom_bridge.c" "$ROOT/third_party/doomgeneric/raven_doom_input.c" \
  "$ROOT/third_party/doomgeneric/doomgeneric_ravenos.c" "$ROOT/tests/doom-key-loss-test.c" -o "$tmp/doom-key-loss-test"
"$tmp/doom-key-loss-test"

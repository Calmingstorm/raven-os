#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -O2 \
  -I"$ROOT/third_party/doomgeneric" "$ROOT/third_party/doomgeneric/raven_doom_input.c" "$ROOT/tests/doom-input-test.c" \
  -o "$tmp/doom-input-test"
"$tmp/doom-input-test"

#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -O2 -fno-builtin -DKEYBOARD_TEST \
  -I"$ROOT/kernel" "$ROOT/kernel/keyboard.c" "$ROOT/tests/keyboard-test.c" \
  -o "$tmp/keyboard-test"
"$tmp/keyboard-test"

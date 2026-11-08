#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TMP="$(mktemp -d /tmp/raven-serial-editor.XXXXXX)"
trap 'rm -rf "$TMP"' EXIT
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -g -O1 \
  -fsanitize=address,undefined -fno-omit-frame-pointer -fno-builtin -ffunction-sections -fdata-sections \
  "$ROOT/tests/serial-editor-test.c" "$ROOT/kernel/string.c" \
  -Wl,--gc-sections -o "$TMP/serial-editor-test"
"$TMP/serial-editor-test"

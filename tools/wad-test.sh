#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
cd "$ROOT"
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -g -O1 -fno-builtin \
    -fsanitize=address,undefined -fno-omit-frame-pointer -Derrno=raven_test_errno -Ikernel \
    tests/wad_native.c kernel/raven_wad.c kernel/storage.c -o "$TMP/wad-native"
"$TMP/wad-native"

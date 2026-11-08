#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TMP="$(mktemp -d "${TMPDIR:-/tmp}/raven-vga-test.XXXXXX")"
trap 'rm -rf "$TMP"' EXIT
CC="${CC:-cc}"
"$CC" -std=c11 -Wall -Wextra -Werror -g -O1 -fsanitize=address,undefined \
    -fno-omit-frame-pointer "$ROOT/tests/vga_test.c" -o "$TMP/vga-test"
"$TMP/vga-test"
# Also compile the unmodified production hardware path with kernel flags.
"$CC" -m32 -ffreestanding -fno-pie -fno-stack-protector -fno-builtin \
    -nostdlib -Wall -Wextra -Werror -I"$ROOT/kernel" \
    -c "$ROOT/kernel/vga.c" -o "$TMP/vga.o"
echo 'vga: freestanding i386 production compile passed'

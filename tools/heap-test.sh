#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TMP="$(mktemp -d /tmp/raven-heap-host.XXXXXX)"
trap 'rm -rf "$TMP"' EXIT
CC="${CC:-gcc}"
"$CC" -std=gnu11 -Wall -Wextra -Werror -g -O1 -fno-omit-frame-pointer \
  -fno-pie -no-pie -fsanitize=address,undefined -fno-sanitize-recover=all \
  "$ROOT/tests/heap-native.c" -o "$TMP/heap-native"
"$TMP/heap-native"
# Also enforce the production ABI/warnings without touching shared build/.
"$CC" -m32 -ffreestanding -fno-pie -fno-stack-protector -fno-builtin \
  -nostdlib -Wall -Wextra -Werror -I"$ROOT/kernel" \
  -c "$ROOT/kernel/heap.c" -o "$TMP/heap-i386.o"

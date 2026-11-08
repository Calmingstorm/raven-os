#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="$(mktemp -d)"
trap 'rm -rf "$BUILD"' EXIT
cd "$ROOT"
as --32 libc/setjmp.s -o "$BUILD/setjmp.o"
as --32 tests/setjmp-registers.s -o "$BUILD/registers.o"
ld -m elf_i386 -nostdlib -e _start "$BUILD/registers.o" "$BUILD/setjmp.o" -o "$BUILD/registers"
"$BUILD/registers"
echo 'setjmp: exact i386 saved-state/live EBX ESI EDI EBP ESP EIP; 0, positive, negative, INT_MIN PASS'
as --32 tests/setjmp-start.s -o "$BUILD/start.o"
for OPT in 0 2 3; do
    gcc -m32 -O"$OPT" -std=c11 -ffreestanding -fno-builtin -fno-pie \
        -fno-stack-protector -nostdlib -nostdinc \
        -isystem "$(gcc -print-file-name=include)" -Ilibc/include \
        -Wall -Wextra -Werror -c tests/setjmp-c.c -o "$BUILD/test.o"
    ld -m elf_i386 -nostdlib -e _start "$BUILD/start.o" "$BUILD/test.o" "$BUILD/setjmp.o" -o "$BUILD/test"
    "$BUILD/test"
    echo "setjmp: C ABI/returns_twice/nested live frames -O$OPT PASS"
done

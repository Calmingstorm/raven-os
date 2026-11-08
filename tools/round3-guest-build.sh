#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="${1:?Usage: round3-guest-build.sh EMPTY_DIR}"
mkdir -p "$OUT"
[[ -z "$(ls -A "$OUT")" ]] || { echo 'Output directory must be empty' >&2; exit 1; }
OUT="$(cd "$OUT" && pwd)"
cd "$ROOT"
flags=(-m32 -O2 -ffreestanding -fno-pie -fno-stack-protector -fno-builtin -nostdlib -Wall -Wextra -Werror -Ikernel)
as --32 boot/boot.s -o "$OUT/boot.o"
as --32 kernel/isr.s -o "$OUT/isr.o"
as --32 libc/setjmp.s -o "$OUT/setjmp.o"
objects=("$OUT/boot.o" "$OUT/isr.o" "$OUT/setjmp.o")
for src in kernel/*.c; do
    [[ "$src" != kernel/kernel.c ]] || continue
    obj="$OUT/$(basename "${src%.c}").o"
    gcc "${flags[@]}" -c "$src" -o "$obj"
    objects+=("$obj")
done
for src in libc/*.c; do
    obj="$OUT/libc-$(basename "${src%.c}").o"
    gcc "${flags[@]}" -msoft-float -mno-sse -mno-mmx -nostdinc -isystem "$(gcc -print-file-name=include)" -Ilibc/include -c "$src" -o "$obj"
    objects+=("$obj")
done
for mode in 0 1; do
    gcc "${flags[@]}" -Wno-unused-function -DROUND3_MODE="$mode" -c tests/runtime-guest.c -o "$OUT/guest-$mode.o"
    ld -m elf_i386 -T boot/linker.ld -nostdlib "${objects[@]}" "$OUT/guest-$mode.o" -o "$OUT/runtime-$mode.kernel"
    grub-file --is-x86-multiboot "$OUT/runtime-$mode.kernel"
done
echo "round3 guest kernels: $OUT"

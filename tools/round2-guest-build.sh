#!/usr/bin/env bash
# Never touches shared build/. Caller owns this disposable output directory.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="${1:?Usage: tools/round2-guest-build.sh EMPTY_OUTPUT_DIRECTORY}"
mkdir -p "$OUT"
[[ -z "$(ls -A "$OUT")" ]] || { echo "Output directory must be empty: $OUT" >&2; exit 1; }
OUT="$(cd "$OUT" && pwd)"
as --32 "$ROOT/boot/boot.s" -o "$OUT/boot.o"
as --32 "$ROOT/kernel/isr.s" -o "$OUT/isr.o"
flags=(-m32 -O2 -ffreestanding -fno-pie -fno-stack-protector -fno-builtin -nostdlib -Wall -Wextra -Werror -I"$ROOT/kernel")
objects=("$OUT/boot.o" "$OUT/isr.o")
for name in bootinfo pmm paging heap wyrd idt gdt serial ports string; do
    gcc "${flags[@]}" -c "$ROOT/kernel/$name.c" -o "$OUT/$name.o"
    objects+=("$OUT/$name.o")
done
for mode in 0 1 2; do
    case "$mode" in 0) name=integrated;; 1) name=fault;; 2) name=early-fault;; esac
    gcc "${flags[@]}" -DROUND2_MODE="$mode" -c "$ROOT/tests/round2-guest.c" -o "$OUT/guest-$name.o"
    ld -m elf_i386 -T "$ROOT/boot/linker.ld" -nostdlib "${objects[@]}" "$OUT/guest-$name.o" -o "$OUT/$name.kernel"
    grub-file --is-x86-multiboot "$OUT/$name.kernel"
done
echo "round2 guest kernels: $OUT"

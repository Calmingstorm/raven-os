#!/usr/bin/env bash
# OUT must be an existing private mktemp directory, not the shared build tree.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="${1:?usage: bash tools/round2-paging-build.sh OUT [MODE]}"
MODE="${2:-0}"
[[ -d "$OUT" && "$MODE" =~ ^[0-6]$ ]]
OUT="$(cd "$OUT" && pwd)"
[[ "$OUT" != "$ROOT" && "$OUT" != "$ROOT/build" ]]
mkdir -p "$OUT/mode-$MODE"
DEST="$OUT/mode-$MODE"
as --32 "$ROOT/boot/boot.s" -o "$DEST/boot.o"
as --32 "$ROOT/kernel/isr.s" -o "$DEST/isr.o"
for src in kernel/bootinfo.c kernel/pmm.c kernel/paging.c kernel/idt.c kernel/gdt.c kernel/serial.c tests/round2-paging-guest.c; do
  gcc -m32 -O1 -ffreestanding -fno-pie -fno-stack-protector -fno-builtin -nostdlib \
    -Wall -Wextra -Werror -DROUND2_MODE="$MODE" -I"$ROOT/kernel" \
    -c "$ROOT/$src" -o "$DEST/$(basename "${src%.c}").o"
done
ld -m elf_i386 -T "$ROOT/boot/linker.ld" -nostdlib \
  "$DEST/boot.o" "$DEST/isr.o" "$DEST/bootinfo.o" "$DEST/pmm.o" \
  "$DEST/paging.o" "$DEST/idt.o" "$DEST/gdt.o" "$DEST/serial.o" \
  "$DEST/round2-paging-guest.o" -o "$DEST/round2-paging.kernel"
grub-file --is-x86-multiboot "$DEST/round2-paging.kernel"
sha256sum "$DEST/round2-paging.kernel"

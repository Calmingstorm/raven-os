#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TMP="$(mktemp -d /tmp/raven-memory-qemu.XXXXXX)"
trap 'rm -rf "$TMP"' EXIT
mkdir -p "$TMP/iso/boot/grub"
as --32 "$ROOT/boot/boot.s" -o "$TMP/boot.o"
for src in kernel/bootinfo.c kernel/pmm.c tests/memory-qemu.c; do
  gcc -m32 -ffreestanding -fno-pie -fno-stack-protector -fno-builtin -nostdlib \
    -Wall -Wextra -Werror -I"$ROOT/kernel" -c "$ROOT/$src" -o "$TMP/$(basename "${src%.c}").o"
done
ld -m elf_i386 -T "$ROOT/boot/linker.ld" -nostdlib \
  "$TMP/boot.o" "$TMP/bootinfo.o" "$TMP/pmm.o" "$TMP/memory-qemu.o" -o "$TMP/iso/boot/memory.kernel"
grub-file --is-x86-multiboot "$TMP/iso/boot/memory.kernel"
nm "$TMP/iso/boot/memory.kernel" | grep -q '^00100000 . kernel_start$'
printf 'set timeout=0\nset default=0\nmenuentry "memory regression" {\n multiboot /boot/memory.kernel\n boot\n}\n' > "$TMP/iso/boot/grub/grub.cfg"
grub-mkrescue -o "$TMP/memory.iso" "$TMP/iso" > "$TMP/grub.log" 2>&1
for memory in 32 128 256; do
  code=0
  timeout 20 qemu-system-i386 -cdrom "$TMP/memory.iso" -m "${memory}M" \
    -display none -serial stdio -monitor none -no-reboot \
    -device isa-debug-exit,iobase=0xf4,iosize=0x04 > "$TMP/serial-${memory}.log" 2>&1 || code=$?
  cat "$TMP/serial-${memory}.log"
  [[ "$code" == 33 ]]
  grep -q 'MEMORY PASS all assertions=' "$TMP/serial-${memory}.log"
  echo "memory QEMU regression: ${memory} MiB passed (no writable disk)"
done

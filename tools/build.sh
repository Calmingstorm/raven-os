#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="$ROOT/build"
ISO="$ROOT/iso"
mkdir -p "$BUILD" "$ISO/boot/grub"
CFLAGS=(-m32 -ffreestanding -fno-pie -fno-stack-protector -fno-builtin -nostdlib -Wall -Wextra -Werror -Ikernel)
ASFLAGS=(--32)
LDFLAGS=(-m elf_i386 -T boot/linker.ld -nostdlib)
cd "$ROOT"
as "${ASFLAGS[@]}" boot/boot.s -o "$BUILD/boot.o"
as "${ASFLAGS[@]}" kernel/isr.s -o "$BUILD/isr.o"
objs=("$BUILD/boot.o" "$BUILD/isr.o")
for src in kernel/*.c; do
  obj="$BUILD/$(basename "${src%.c}").o"
  gcc "${CFLAGS[@]}" -c "$src" -o "$obj"
  objs+=("$obj")
done
ld "${LDFLAGS[@]}" "${objs[@]}" -o "$BUILD/ravenos.kernel"
grub-file --is-x86-multiboot "$BUILD/ravenos.kernel"
cp "$BUILD/ravenos.kernel" "$ISO/boot/ravenos.kernel"
cat > "$ISO/boot/grub/grub.cfg" <<'EOF'
set timeout=0
set default=0
menuentry "RavenOS" {
    multiboot /boot/ravenos.kernel
    boot
}
EOF
grub-mkrescue -o "$BUILD/ravenos.iso" "$ISO" >/dev/null 2>&1
echo "kernel: $BUILD/ravenos.kernel"
echo "iso:    $BUILD/ravenos.iso"
echo "multiboot: ok"

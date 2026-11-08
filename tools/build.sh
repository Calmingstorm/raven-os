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
# Runtime libc is a separate freestanding include domain. Kernel flags above
# stay unchanged; no host glibc headers or floating-point ABI enter the image.
LIBCFLAGS=(-m32 -O2 -ffreestanding -fno-pie -fno-stack-protector -fno-builtin
  -nostdlib -nostdinc -isystem "$(gcc -print-file-name=include)"
  -Ilibc/include -Ikernel -msoft-float -mno-sse -mno-mmx -Wall -Wextra -Werror)
for src in libc/*.c; do
  obj="$BUILD/libc-$(basename "${src%.c}").o"
  gcc "${LIBCFLAGS[@]}" -c "$src" -o "$obj"
  objs+=("$obj")
done
as "${ASFLAGS[@]}" libc/setjmp.s -o "$BUILD/libc-setjmp.o"
objs+=("$BUILD/libc-setjmp.o")
# The Doom engine (GPL-2.0, third_party/doomgeneric) and its RavenOS port: the libc include domain and its no-FPU
# flags. Upstream's own code isn't held to -Werror, but an implicit declaration still fails the build.
DOOMFLAGS=(-m32 -O2 -ffreestanding -fno-pie -fno-stack-protector -fno-builtin -nostdlib -nostdinc
  -isystem "$(gcc -print-file-name=include)" -Ilibc/include -Ikernel -Ithird_party/doomgeneric
  -msoft-float -mno-sse -mno-mmx -DRAVENOS -Werror=implicit-function-declaration)
for src in third_party/doomgeneric/*.c; do
  obj="$BUILD/doom-$(basename "${src%.c}").o"
  gcc "${DOOMFLAGS[@]}" -c "$src" -o "$obj"
  objs+=("$obj")
done
# Doom's FixedDiv divides 64 bits by 32. libgcc's __divdi3 object is the one compiler helper in the image; nothing
# else is taken from libgcc. Where it came from is recorded beside the build.
libgcc="$(gcc -m32 -print-libgcc-file-name)"
rm -f "$BUILD/_divdi3.o"
(cd "$BUILD" && ar x "$libgcc" _divdi3.o) 2>/dev/null || true
if ! readelf -h "$BUILD/_divdi3.o" 2>/dev/null | grep -q 'Machine:.*Intel 80386'; then
  echo "build: no 32-bit __divdi3 in $libgcc; install gcc-multilib" >&2
  exit 1
fi
{
  echo "gcc: $(gcc --version | head -n1)"
  echo "archive: $libgcc"
  echo "package: $(dpkg-query -S "$libgcc" 2>/dev/null | cut -d: -f1 | xargs -r dpkg-query -W -f='${Package} ${Version}' 2>/dev/null || echo unknown)"
  echo "member: _divdi3.o, sha256 $(sha256sum "$BUILD/_divdi3.o" | cut -d' ' -f1)"
} > "$BUILD/libgcc-divdi3.txt"
objs+=("$BUILD/_divdi3.o")
ld "${LDFLAGS[@]}" -Map="$BUILD/ravenos.map" "${objs[@]}" -o "$BUILD/ravenos.kernel"
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

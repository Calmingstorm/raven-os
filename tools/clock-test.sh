#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="$(mktemp -d)"
trap 'rm -rf "$BUILD"' EXIT
cd "$ROOT"
gcc -O2 -std=c11 -Wall -Wextra -Werror -ffunction-sections -fdata-sections \
    tests/clock-native.c -Wl,--gc-sections -o "$BUILD/clock-native"
"$BUILD/clock-native"
gcc -m32 -O2 -ffreestanding -fno-builtin -fno-pie -fno-stack-protector \
    -nostdlib -Wall -Wextra -Werror -Ikernel -c kernel/raven_clock.c -o "$BUILD/clock.o"
objdump -d "$BUILD/clock.o" > "$BUILD/disassembly"
python3 - "$BUILD/disassembly" <<'PY'
import pathlib, re, sys
text = pathlib.Path(sys.argv[1]).read_text()
instructions = re.findall(r'^\s*[0-9a-f]+:\s+(?:[0-9a-f]{2}\s+)+\s*(\w+)', text, re.M)
assert any(a == 'sti' and b == 'hlt' for a, b in zip(instructions, instructions[1:])), text
assert 'cli' in instructions and 'ud2' in instructions, text
print('clock: real i386 CLI/check/STI-HLT instruction seam compiled, adjacent STI/HLT PASS')
PY
if nm -u "$BUILD/clock.o" | grep -E 'keyboard|shell'; then
    echo 'clock unexpectedly calls input/shell code' >&2
    exit 1
fi

# The full-duration simulation above is complemented by real privileged
# instructions and timer wakeups in a disposable guest, never the desktop.
as --32 boot/boot.s -o "$BUILD/boot.o"
as --32 tests/clock-guest-irq.s -o "$BUILD/irq.o"
for SOURCE in tests/clock-guest.c kernel/ports.c; do
    gcc -m32 -O2 -ffreestanding -fno-builtin -fno-pie -fno-stack-protector \
        -nostdlib -Wall -Wextra -Werror -Ikernel -c "$SOURCE" \
        -o "$BUILD/$(basename "${SOURCE%.c}").o"
done
ld -m elf_i386 -T boot/linker.ld -nostdlib "$BUILD/boot.o" "$BUILD/irq.o" \
    "$BUILD/clock-guest.o" "$BUILD/ports.o" "$BUILD/clock.o" -o "$BUILD/guest"
set +e
timeout 15s qemu-system-i386 -accel tcg -m 32 -display none -monitor none \
    -serial stdio -no-reboot -no-shutdown -kernel "$BUILD/guest" \
    -device isa-debug-exit,iobase=0xf4,iosize=0x04 > "$BUILD/guest.log" 2>&1
RESULT=$?
set -e
cat "$BUILD/guest.log"
test "$RESULT" -eq 33
grep -q 'clock: QEMU real PIT/IRQ HLT, IF, tick and ms rollover PASS' "$BUILD/guest.log"

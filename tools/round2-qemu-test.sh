#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TMP="$(mktemp -d /tmp/raven-round2-qemu.XXXXXX)"
trap 'if [[ "${ROUND2_KEEP:-0}" == 1 ]]; then echo "Retained $TMP"; else rm -rf "$TMP"; fi' EXIT
bash "$ROOT/tools/round2-guest-build.sh" "$TMP/kernels"
mkdir -p "$TMP/iso/boot/grub"
for mode in integrated fault early-fault; do
    cp "$TMP/kernels/$mode.kernel" "$TMP/iso/boot/guest.kernel"
    printf 'set timeout=0\nset default=0\nmenuentry "round2" {\n multiboot /boot/guest.kernel\n boot\n}\n' > "$TMP/iso/boot/grub/grub.cfg"
    grub-mkrescue -o "$TMP/$mode.iso" "$TMP/iso" > "$TMP/grub-$mode.log" 2>&1
    if [[ "$mode" == integrated ]]; then
        # QEMU >= 9.1 honors -no-shutdown for isa-debug-exit as well, which
        # would leave this successful guest stopped instead of returning 33.
        code=0
        timeout --kill-after=2 25 qemu-system-i386 -cdrom "$TMP/$mode.iso" -m 128M \
            -display none -serial stdio -monitor none -no-reboot \
            -device isa-debug-exit,iobase=0xf4,iosize=0x04 > "$TMP/$mode.log" 2>&1 || code=$?
        cat "$TMP/$mode.log"
        [[ "$code" == 33 ]]
        grep -q 'ROUND2 PASS all assertions=' "$TMP/$mode.log"
    else
        node "$ROOT/tools/round2-guest-qemu-fault.mjs" "$TMP/$mode.iso" "$TMP/$mode.log"
    fi
    echo "round2 QEMU: $mode passed (no writable disk)"
done

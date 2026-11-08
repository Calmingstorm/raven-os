#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TMP="$(mktemp -d /tmp/raven-round3-qemu.XXXXXX)"
trap 'if [[ "${ROUND3_KEEP:-0}" == 1 ]]; then echo "Retained $TMP"; else rm -rf "$TMP"; fi' EXIT
bash "$ROOT/tools/round3-guest-build.sh" "$TMP/kernels"
python3 "$ROOT/tools/round3-fixtures.py" "$TMP/images"
mkdir -p "$TMP/iso/boot/grub"
for scenario in valid corrupt short missing small-master; do
    mode=1; slave="$scenario"; master=master
    case "$scenario" in valid) mode=0;; small-master) mode=0; slave=valid; master=small-master;; esac
    cp "$TMP/kernels/runtime-$mode.kernel" "$TMP/iso/boot/guest.kernel"
    printf 'set timeout=0\nset default=0\nmenuentry "round3" {\n multiboot /boot/guest.kernel\n boot\n}\n' > "$TMP/iso/boot/grub/grub.cfg"
    grub-mkrescue -o "$TMP/$scenario.iso" "$TMP/iso" >/dev/null 2>&1
    before="$(sha256sum "$TMP/images/$master.img")"
    drives=(-drive "file=$TMP/images/$master.img,format=raw,if=ide,index=0")
    if [[ "$scenario" != missing ]]; then
        slave_before="$(sha256sum "$TMP/images/$slave.img")"
        # QEMU ide-hd refuses readonly=on ("Block node is read-only"). A
        # disposable snapshot keeps the raw backing image read-only while
        # exposing normal IDE hardware. Driver tests separately prove no slave
        # writes exist; do not pretend this is hardware write protection.
        drives+=(-drive "file=$TMP/images/$slave.img,format=raw,if=ide,index=1,snapshot=on")
    fi
    code=0
    timeout --kill-after=2 45 qemu-system-i386 -cdrom "$TMP/$scenario.iso" -m 128M \
      -display none -serial stdio -monitor none -no-reboot \
      -device isa-debug-exit,iobase=0xf4,iosize=0x04 "${drives[@]}" > "$TMP/$scenario.log" 2>&1 || code=$?
    cat "$TMP/$scenario.log"
    [[ "$code" == 33 ]]
    grep -q 'ROUND3 PASS all assertions=' "$TMP/$scenario.log"
    [[ "$before" == "$(sha256sum "$TMP/images/$master.img")" ]]
    if [[ "$scenario" != missing ]]; then
        [[ "$slave_before" == "$(sha256sum "$TMP/images/$slave.img")" ]]
    fi
    echo "round3 QEMU: $scenario passed; master SHA-256 unchanged"
done

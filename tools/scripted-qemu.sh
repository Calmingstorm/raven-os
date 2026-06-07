#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SCRIPT="${1:-$ROOT/tests/command_script.txt}"
ISO="$ROOT/build/ravenos.iso"
if [ ! -f "$ISO" ]; then
  "$ROOT/tools/build.sh" >/dev/null
fi
# This is intentionally a smoke harness placeholder: QEMU's VGA shell is interactive,
# and keyboard injection is host/backend sensitive. The canonical automated validation
# remains smoke-qemu plus compile checks until serial console support lands.
qemu-system-i386 -cdrom "$ISO" -m 128M -no-reboot -display none -serial stdio -monitor none < "$SCRIPT"

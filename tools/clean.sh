#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
rm -rf "$ROOT/build" "$ROOT/iso/boot/ravenos.kernel" "$ROOT/iso/boot/grub/grub.cfg"
echo "cleaned"

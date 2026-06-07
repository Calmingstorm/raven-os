#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
"$ROOT/tools/build-disk.sh" >/dev/null
exec qemu-system-i386 \
  -cdrom "$ROOT/build/ravenos.iso" \
  -drive file="$ROOT/build/ravenos-disk.img",format=raw,if=ide,index=0,media=disk \
  -m 128M -serial stdio

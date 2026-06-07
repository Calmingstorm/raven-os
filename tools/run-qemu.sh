#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
"$ROOT/tools/build.sh"
exec qemu-system-i386 -cdrom "$ROOT/build/ravenos.iso" -m 128M -serial stdio

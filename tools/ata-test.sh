#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
cc=${CC:-cc}
flags=(-std=c11 -Wall -Wextra -Werror -fno-builtin -DRAVEN_ATA_POLL_LIMIT=64 -I"$root/kernel")
"$cc" "${flags[@]}" -O2 "$root/kernel/ata.c" "$root/tests/ata_ports.c" -o "$tmp/ata-test"
"$tmp/ata-test"
"$cc" "${flags[@]}" -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
    "$root/kernel/ata.c" "$root/tests/ata_ports.c" -o "$tmp/ata-test-sanitize"
ASAN_OPTIONS=detect_leaks=0 "$tmp/ata-test-sanitize"

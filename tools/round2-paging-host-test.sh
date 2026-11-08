#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TMP="$(mktemp -d /tmp/raven-round2-paging-host.XXXXXX)"
trap 'rm -rf "$TMP"' EXIT
gcc -std=gnu11 -Wall -Wextra -Werror -g -O1 -fno-pie -no-pie \
  -ffunction-sections -fdata-sections -Wl,--gc-sections \
  -fsanitize=undefined -fno-sanitize-recover=all \
  -Wl,--defsym=kernel_start=0x100000,--defsym=kernel_end=0x180000 \
  "$ROOT/tests/round2-paging-host.c" -o "$TMP/round2-paging-host"
"$TMP/round2-paging-host"

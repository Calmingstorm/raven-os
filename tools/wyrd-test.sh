#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TMP="$(mktemp -d /tmp/raven-wyrd.XXXXXX)"
trap 'rm -rf "$TMP"' EXIT
gcc -std=gnu11 -Wall -Wextra -Werror -g -O1 -fno-builtin -fno-pie -no-pie \
  -fsanitize=address,undefined -fno-sanitize-recover=all \
  "$ROOT/tests/wyrd-test.c" "$ROOT/kernel/wyrd.c" -o "$TMP/wyrd-test"
"$TMP/wyrd-test"

#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
# Do not add -Ilibc/include: a native test must use the host headers and setjmp
# ABI, not silently overwrite its own stdio/stdlib definitions.
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -O1 -g \
  -fno-omit-frame-pointer -fno-pie -no-pie -fsanitize=address,undefined \
  -DRAVEN_RUNTIME_TEST \
  "$ROOT/kernel/raven_doom_runtime.c" "$ROOT/tests/runtime-test.c" \
  -o "$tmp/runtime-test"
ulimit -c 0
ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 "$tmp/runtime-test"

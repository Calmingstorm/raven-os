#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
export ASAN_OPTIONS=detect_leaks=1:abort_on_error=1
export UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1
common=(-std=c11 -Wall -Wextra -Werror -O1 -g -fno-omit-frame-pointer
        -fno-pie -no-pie -fsanitize=address,undefined -DRAVEN_RUNTIME_TEST)
sources=("$ROOT/kernel/raven_doom_runtime.c" "$ROOT/kernel/doom_command.c"
         "$ROOT/tests/doom-command-test.c")
for variant in absent main-only cleanup-only linked; do
  defines=()
  case "$variant" in
    main-only) defines=(-DHAVE_PORT_MAIN) ;;
    cleanup-only) defines=(-DHAVE_PORT_CLEANUP) ;;
    linked) defines=(-DHAVE_PORT_MAIN -DHAVE_PORT_CLEANUP) ;;
  esac
  "${CC:-cc}" "${common[@]}" "${defines[@]}" "${sources[@]}" -o "$tmp/$variant"
  if [[ "$variant" == linked ]]; then
    for mode in return exit0 exitpositive exitnegative abort; do
      "$tmp/$variant" "$mode"
    done
  else
    "$tmp/$variant"
  fi
done

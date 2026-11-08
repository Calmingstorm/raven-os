#!/bin/sh
# Does not build: callers must finish their build first.
set -eu
exec python3 "$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)/console-qemu-test.py" "$@"

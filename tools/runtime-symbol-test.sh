#!/usr/bin/env bash
# Audits the linked image. tools/doom-audit.sh audits the engine objects themselves.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
KERNEL="${1:-$ROOT/build/ravenos.kernel}"
[[ -f "$KERNEL" ]] || { echo "Build first: $KERNEL" >&2; exit 1; }
TMP="$(mktemp -d /tmp/raven-runtime-symbols.XXXXXX)"
trap 'rm -rf "$TMP"' EXIT
nm -u "$KERNEL" > "$TMP/undefined"
if grep -vE '^[[:space:]]*[wv][[:space:]]+raven_doom_port_(main|cleanup)$' "$TMP/undefined"; then
    echo 'Unexpected unresolved runtime symbols' >&2
    exit 1
fi
for symbol in setjmp longjmp malloc calloc realloc free snprintf vsnprintf sscanf fopen fread fseek raven_doom_run raven_wad_mount raven_clock_ms raven_sleep_ms; do
    nm --defined-only "$KERNEL" | awk '{print $3}' > "$TMP/defined"
    grep -qx "$symbol" "$TMP/defined"
done
# __divdi3 is the one compiler helper allowed: Doom's FixedDiv needs it, and doom-audit.sh checks it is libgcc's
# own object and the only thing taken from libgcc.
if nm "$KERNEL" | grep -vE '[[:space:]]__divdi3$' | grep -E '\b(__.*(df|sf|di)3|atof|strtod|system|fork|execve)$'; then
    echo 'Unexpected float/compiler-runtime or host-process dependency' >&2
    exit 1
fi
# objdump decodes instructions, avoiding byte-pattern false positives in data.
objdump -d --no-show-raw-insn "$KERNEL" > "$TMP/disassembly"
if grep -E '^[[:space:]]*[[:xdigit:]]+:[[:space:]]+(f(ld|st|add|sub|mul|div|com|ucom|ild|ist|init|ninit|wait|chs|abs|sqrt|xch)|[a-z0-9]+[[:space:]].*%(xmm|ymm|mm)[0-9])' "$TMP/disassembly"; then
    echo 'Unexpected x87/SIMD use in runtime kernel' >&2
    exit 1
fi
echo 'runtime symbols: resolved freestanding integer-only image, no host/float helpers beyond __divdi3'

#!/usr/bin/env bash
# Audit the Doom engine as tools/build.sh linked it (run that first):
# - the engine objects import exactly the agreed surface: the runtime libc, the runtime's clock and session
#   boundary, the port's kernel bridge, and __divdi3, nothing else (no atof, no maths, no float helpers);
# - libgcc contributes only its __divdi3 object to the image;
# - no engine object (or that one) contains an x87, MMX or SSE instruction;
# - no format string the engine compiles with uses a float conversion or %n, which RavenOS's printf refuses.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="$ROOT/build"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
shopt -s nullglob
engine=("$BUILD"/doom-*.o)
[ "${#engine[@]}" -gt 0 ] && [ -f "$BUILD/ravenos.map" ] || { echo "doom-audit: build first (tools/build.sh)" >&2; exit 1; }
[ "${#engine[@]}" -eq "$(ls "$ROOT"/third_party/doomgeneric/*.c | wc -l)" ] || { echo "doom-audit: stale engine objects in $BUILD" >&2; exit 1; }

ld -m elf_i386 -r -o "$TMP/engine.o" "${engine[@]}"
nm -u "$TMP/engine.o" | awk '{print $2}' | sort > "$TMP/imports"
sort > "$TMP/allowed" <<'EOF'
__divdi3
abs
atoi
calloc
errno
exit
fclose
fflush
fopen
fprintf
fread
free
fseek
ftell
fwrite
malloc
memcpy
memset
mkdir
printf
putchar
puts
raven_clock_ms
raven_doom_abort
raven_doom_exit
raven_doom_key_held
raven_doom_key_losses
raven_doom_key_next
raven_doom_video_begin
raven_doom_video_check
raven_doom_video_end
raven_sleep_ms
realloc
remove
rename
snprintf
sscanf
stderr
stdout
strcasecmp
strcmp
strdup
strlen
strncasecmp
strncmp
strncpy
strrchr
strstr
toupper
vfprintf
vsnprintf
EOF
if ! diff -u "$TMP/allowed" "$TMP/imports" > "$TMP/imports.diff"; then
  cat "$TMP/imports.diff" >&2
  echo "doom-audit: the engine's imports are not the agreed surface (+ new, - no longer used)" >&2
  exit 1
fi

# The map names every archive member the linker pulled in; libgcc's __divdi3 is extracted beforehand, so libgcc.a
# itself must not appear at all.
if grep -q 'libgcc' "$BUILD/ravenos.map"; then
  grep 'libgcc' "$BUILD/ravenos.map" >&2
  echo "doom-audit: the image takes something from libgcc.a" >&2
  exit 1
fi
grep -q '_divdi3\.o' "$BUILD/ravenos.map" || { echo "doom-audit: __divdi3 isn't libgcc's own object" >&2; exit 1; }
readelf -h "$BUILD/_divdi3.o" | grep -q 'Class:.*ELF32' || { echo "doom-audit: _divdi3.o is not 32-bit" >&2; exit 1; }
[ "$(nm --defined-only "$BUILD/_divdi3.o" | awk '{print $2 " " $3}')" = "T __divdi3" ] && [ -z "$(nm -u "$BUILD/_divdi3.o")" ] ||
  { echo "doom-audit: _divdi3.o defines or needs more than __divdi3" >&2; exit 1; }

# Every x87 instruction's mnemonic starts with f (and AVX's with v); MMX and SSE show in their registers or state
# instructions. Decide first, then show a few: a pipe into head could mask the result.
objdump -d --no-show-raw-insn "${engine[@]}" "$BUILD/_divdi3.o" > "$TMP/disassembly"
if grep -E '^[[:space:]]*[[:xdigit:]]+:[[:space:]]+((f|v)[a-z0-9]*|emms|wait|ldmxcsr|stmxcsr)([[:space:]]|$)|%(x|y|z)?mm[0-9]' "$TMP/disassembly" > "$TMP/fpu"; then
  head -n 20 "$TMP/fpu" >&2
  echo "doom-audit: x87 or SIMD instructions in the engine ($(wc -l < "$TMP/fpu") lines)" >&2
  exit 1
fi

# Format strings as the compiler sees them, with RavenOS's macros and adjacent literals joined ("%" "f" is "%f"):
# no %f/%e/%g/%a and no %n. Escaped percent signs (%%) are literal text.
for src in "$ROOT"/third_party/doomgeneric/*.c; do
  gcc -m32 -E -P -ffreestanding -nostdinc -isystem "$(gcc -print-file-name=include)" -I"$ROOT/libc/include" \
    -I"$ROOT/kernel" -I"$ROOT/third_party/doomgeneric" -DRAVENOS "$src" |
    python3 -c '
import re, sys
literal = r"\"(?:[^\"\\\n]|\\.)*\""
conversion = re.compile(r"%[-+ #0]*[0-9*]*(?:\.[0-9*]*)?(?:hh|h|ll|l|L|z|j|t)?[fFeEgGaAn]")
for run in re.finditer(r"(?:%s\s*)+" % literal, sys.stdin.read()):
    joined = "".join(part[1:-1] for part in re.findall(literal, run.group(0)))
    if conversion.search(joined.replace("%%", "")):
        print(sys.argv[1] + ": " + joined)
' "$(basename "$src")"
done > "$TMP/formats"
if [ -s "$TMP/formats" ]; then
  cat "$TMP/formats" >&2
  echo "doom-audit: float or %n conversions in engine format strings" >&2
  exit 1
fi
echo "doom-audit: ${#engine[@]} engine objects import exactly the agreed surface; only libgcc's __divdi3 object is linked; no x87/SIMD; no float or %n formats"
sed 's/^/  /' "$BUILD/libgcc-divdi3.txt"

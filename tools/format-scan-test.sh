#!/usr/bin/env bash
# tools/format-scan.py checked against the compiler: the bytes it decodes for every string literal must be the
# bytes GCC emits for it, and exactly the strings that are float or %n formats must be reported.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
cat > "$tmp/fixtures.c" <<'EOF'
/* Formats RavenOS's printf refuses, spelled in the ways C allows. */
const char *f1 = "\x025" "f";
const char *f2 = "\x000025" "f";
const char *f3 = "%\x000066";
const char *f4 = "\x000025" "n";
const char *f5 = "€\045f";
const char *f6 = "€\045f";
const char *f7 = "\045" "5.2f";
const char *f8 = "%%%e";
/* Strings that only look like them. */
const char *n1 = "\\045f";
const char *n2 = "100%%f";
const char *n3 = "\x25\x25" "f";
const char *n4 = "%d %s %x";
EOF
gcc -m32 -O2 -c "$tmp/fixtures.c" -o "$tmp/fixtures.o"
# The compiler's bytes: every string in the object's string sections.
for section in $(readelf -SW "$tmp/fixtures.o" | grep -o '\.rodata\.str[^ ]*'); do
  objcopy -O binary --only-section="$section" "$tmp/fixtures.o" "$tmp/section.bin"
  python3 -c 'import sys; [print(s.hex()) for s in open(sys.argv[1], "rb").read().split(b"\0") if s]' "$tmp/section.bin"
done | sort -u > "$tmp/compiled"
gcc -m32 -E -P "$tmp/fixtures.c" | python3 "$ROOT/tools/format-scan.py" --decode | sort -u > "$tmp/decoded"
[ "$(wc -l < "$tmp/decoded")" -eq 9 ] || { echo "format-scan-test: expected 9 distinct strings" >&2; cat "$tmp/decoded" >&2; exit 1; }
if [ -n "$(comm -23 "$tmp/decoded" "$tmp/compiled")" ]; then
  echo "format-scan-test: decoded bytes the compiler didn't emit:" >&2
  comm -23 "$tmp/decoded" "$tmp/compiled" >&2
  exit 1
fi
gcc -m32 -E -P "$tmp/fixtures.c" | python3 "$ROOT/tools/format-scan.py" fixtures | sort > "$tmp/flagged"
printf 'fixtures: %s\n' '%f' '%f' '%f' '%n' '€%f' '€%f' '%5.2f' '%%%e' | sort > "$tmp/expected"
if ! diff -u "$tmp/expected" "$tmp/flagged" >&2; then
  echo "format-scan-test: the wrong strings were reported" >&2
  exit 1
fi
echo "format-scan-test: the decoder matches the bytes GCC emits for all 12 strings (9 distinct); the 8 float or %n formats are reported, the 4 look-alikes aren't"

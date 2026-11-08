#!/usr/bin/env bash
# tools/format-scan.py on a compiled fixture: exactly the strings that are float or %n formats must be reported,
# however the source spells them, and none of the look-alikes.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
cat > "$tmp/fixtures.c" <<'EOF'
/* Formats RavenOS's printf refuses, spelled in the ways C allows. */
const char *f1 = "\x025" "f";
const char *f2 = "\x000025" "n";
const char *f3 = "%\x000066" "x";
const char *f4 = "€\045f";
const char *f5 = "\045" "5.2f";
const char *f6 = "%%%e";
const char quote = '"'; const char *f7 = "after a quote: %g";
const char *f8 = u8"%" "a";
const char f9[] = "%7.3G";        /* a const array, in .rodata */
char f10[] = "%-4.1E";            /* a writable array, in .data */
const char f11[] = "\x1b[1m%n";   /* an escape character runs into it */
const struct { char tag; char text[7]; } f12 = { 1, "%-08.2f" };   /* straight after a struct field */
/* Strings that only look like them. */
const char *n1 = "\\045f";
const char *n2 = "100%%f";
const char *n3 = "\x25\x25" "f";
const char *n4 = "%d %s %x %c %p %u %o";
const char n5[] = "100%%";
EOF
gcc -m32 -O2 -c "$tmp/fixtures.c" -o "$tmp/fixtures.o"
python3 "$ROOT/tools/format-scan.py" "$tmp/fixtures.o" | sed 's/^[^:]*: [^:]*: //' | sort > "$tmp/flagged"
printf '%s\n' '%f' '%n' '%fx' '€%f' '%5.2f' '%%%e' 'after a quote: %g' '%a' '%7.3G' '%-4.1E' '[1m%n' '%-08.2f' |
  sort > "$tmp/expected"
if ! diff -u "$tmp/expected" "$tmp/flagged" >&2; then
  echo "format-scan-test: the wrong strings were reported" >&2
  exit 1
fi
echo "format-scan-test: the 12 float or %n formats are reported from the compiled object, however spelled or stored, and the 5 look-alikes aren't"

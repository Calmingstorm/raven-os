#!/usr/bin/env bash
# __divdi3 as the kernel links it: libgcc's own object (build/_divdi3.o, extracted by tools/build.sh; run that
# first), called from a 32-bit program on signed cases whose quotients Python works out independently, and Doom's
# FixedDiv (m_fixed.c, the only caller) on top of it. Nothing else from libgcc is linked into the program.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OBJECT="$ROOT/build/_divdi3.o"
[ -f "$OBJECT" ] || { echo "divdi3-test: no $OBJECT; run tools/build.sh first" >&2; exit 1; }
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

python3 - "$tmp/cases.h" <<'EOF'
import random
import sys

random.seed(20261108)
MIN64, MAX64 = -(1 << 63), (1 << 63) - 1
MIN32, MAX32 = -(1 << 31), (1 << 31) - 1

def trunc_div(a, b):
    q = abs(a) // abs(b)
    return q if (a < 0) == (b < 0) else -q

def wrap32(v):
    return (v + (1 << 31)) % (1 << 32) - (1 << 31)

def fixed_div(a, b):
    # m_fixed.c: if ((abs(a) >> 14) >= abs(b)) return (a^b) < 0 ? INT_MIN : INT_MAX; else (fixed_t)(((int64_t)a << 16) / b)
    if (abs(a) >> 14) >= abs(b):
        return MIN32 if (a ^ b) < 0 else MAX32
    return wrap32(trunc_div(a << 16, b))

pairs = [(0, 1), (1, 1), (-1, 1), (1, -1), (-1, -1), (7, 2), (-7, 2), (7, -2), (-7, -2), (MAX64, 1), (MAX64, -1),
         (MIN64, 1), (MIN64, 2), (MIN64, MAX64), (MAX64, MIN64), (MIN64 + 1, -1), (1 << 32, 1 << 32),
         ((1 << 32) - 1, 1 << 32), (1 << 32, (1 << 32) - 1), (-(1 << 47), 3), (1 << 47, -(1 << 16)),
         (MAX64, (1 << 32) + 1), (MIN64, -((1 << 32) + 1))]
for _ in range(4000):
    a = random.getrandbits(random.randint(1, 63)) * random.choice((1, -1))
    b = 0
    while b == 0:
        b = random.getrandbits(random.randint(1, 63)) * random.choice((1, -1))
    pairs.append((a, b))
fixed = [(65536, 65536), (-65536, 65536), (1, 3), (-(1 << 30), 1), (1 << 20, -3), (3 << 16, 2 << 16), (MAX32, 1 << 16),
         (MIN32 + 1, MAX32), (12345, 0)]
for _ in range(4000):
    a = random.randint(MIN32 + 1, MAX32)
    b = random.randint(MIN32 + 1, MAX32) >> random.randint(0, 30)
    fixed.append((a, b))

with open(sys.argv[1], "w") as out:
    out.write("static const struct { long long a, b, q; } cases[] = {\n")
    for a, b in pairs:
        out.write(f"    {{ {a}LL, {b}LL, {trunc_div(a, b)}LL }},\n".replace("-9223372036854775808LL", "(-9223372036854775807LL - 1)"))
    out.write("};\nstatic const struct { int a, b, q; } fixed_cases[] = {\n")
    for a, b in fixed:
        out.write(f"    {{ {a}, {b}, {fixed_div(a, b)} }},\n".replace("-2147483648", "(-2147483647 - 1)"))
    out.write("};\n")
EOF

cat > "$tmp/divdi3.c" <<'EOF'
#include <stdio.h>
#include "cases.h"

long long __divdi3(long long a, long long b);
typedef int fixed_t;
fixed_t FixedDiv(fixed_t a, fixed_t b);

int main(void) {
    unsigned failures = 0, n = sizeof cases / sizeof *cases, m = sizeof fixed_cases / sizeof *fixed_cases;
    for (unsigned i = 0; i < n; i++) {
        long long q = __divdi3(cases[i].a, cases[i].b);
        if (q != cases[i].q && failures++ < 10) printf("FAIL %lld / %lld = %lld, not %lld\n", cases[i].a, cases[i].b, q, cases[i].q);
    }
    for (unsigned i = 0; i < m; i++) {
        fixed_t q = FixedDiv(fixed_cases[i].a, fixed_cases[i].b);
        if (q != fixed_cases[i].q && failures++ < 10) printf("FAIL FixedDiv(%d, %d) = %d, not %d\n", fixed_cases[i].a, fixed_cases[i].b, q, fixed_cases[i].q);
    }
    if (failures) return 1;
    printf("divdi3-test: libgcc's __divdi3 object right on %u signed cases, and FixedDiv on %u\n", n, m);
    return 0;
}
EOF
# m_fixed.c is compiled as the kernel compiles it, so its 64-bit division really is a call to __divdi3.
gcc -m32 -O2 -ffreestanding -fno-pie -fno-builtin -nostdinc -isystem "$(gcc -print-file-name=include)" \
  -I"$ROOT/libc/include" -I"$ROOT/kernel" -I"$ROOT/third_party/doomgeneric" -msoft-float -mno-sse -mno-mmx -DRAVENOS \
  -c "$ROOT/third_party/doomgeneric/m_fixed.c" -o "$tmp/m_fixed.o"
nm -u "$tmp/m_fixed.o" | grep -q ' __divdi3$' || { echo "divdi3-test: FixedDiv no longer calls __divdi3" >&2; exit 1; }
# No default libraries: libgcc can't supply __divdi3 behind the object's back. abs() comes from the C library.
gcc -m32 -O2 -no-pie -nodefaultlibs -I"$tmp" "$tmp/divdi3.c" "$tmp/m_fixed.o" "$OBJECT" -lc -o "$tmp/divdi3"
"$tmp/divdi3"

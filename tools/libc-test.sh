#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TMP="$(mktemp -d /tmp/raven-libc-host.XXXXXX)"
trap 'rm -rf "$TMP"' EXIT
CC="${CC:-gcc}"
# Prefix every custom public symbol, never interpose on ASan/glibc startup.
symbols=(errno stdout stderr memcpy memset strlen strcmp strncmp memmove memcmp
 strcpy strncpy strchr strrchr strstr strdup strcasecmp strncasecmp malloc calloc
 realloc free abs atoi getenv exit abort isalnum isalpha isdigit isspace isprint
 islower isupper isxdigit tolower toupper fopen fclose fread fwrite fseek ftell
 feof ferror clearerr fflush putchar puts printf fprintf vprintf vfprintf snprintf
 vsnprintf sscanf remove rename mkdir)
renames=()
for symbol in "${symbols[@]}"; do renames+=("-D$symbol=rv_$symbol"); done
native=(-std=gnu11 -Wall -Wextra -Werror -g -O1 -fno-omit-frame-pointer
 -fno-builtin -fno-pie -fsanitize=address,undefined -fno-sanitize-recover=all)
inc=(-nostdinc -isystem "$("$CC" -print-file-name=include)" -I"$ROOT/libc/include" -I"$ROOT/kernel")
objects=()
for source in "$ROOT"/libc/*.c "$ROOT/kernel/string.c" "$ROOT/tests/libc-native.c"; do
 object="$TMP/$(basename "$(dirname "$source")")-$(basename "$source" .c).o"
 "$CC" "${native[@]}" "${inc[@]}" "${renames[@]}" -c "$source" -o "$object"
 objects+=("$object")
done
"$CC" "${native[@]}" -c "$ROOT/tests/libc-host.c" -o "$TMP/host.o"
"$CC" "${native[@]}" -no-pie "${objects[@]}" "$TMP/host.o" -o "$TMP/libc-native"
"$TMP/libc-native"
# Compile every shipped source under real i386 flags, outside shared build/.
for source in "$ROOT"/libc/*.c; do
 object="$TMP/i386-$(basename "$source" .c).o"
 "$CC" -std=gnu11 -O2 -m32 -ffreestanding -fno-builtin -fno-pie \
  -fno-stack-protector -msoft-float -mno-sse -mno-mmx -Wall -Wextra -Werror \
  "${inc[@]}" -c "$source" -o "$object"
done
ld -m elf_i386 -r "$TMP"/i386-*.o -o "$TMP/libc-i386.o"
nm -u "$TMP/libc-i386.o" | tee "$TMP/undefined.txt"
if sed -n 's/^ *U //p' "$TMP/undefined.txt" | grep -Ev \
 '^(memcpy|memset|strlen|strcmp|strncmp|raven_doom_(abort|exit|malloc|calloc|realloc|free)|raven_wad_(open|size|read_at|close)|vga_putc)$'; then
 echo 'unexpected undefined symbol (including compiler-runtime helpers)' >&2; exit 1
fi
if objdump -d "$TMP/libc-i386.o" | grep -Eq '\b(fld|fst|fadd|fsub|fmul|fdiv|fild|fist|xmm|mm[0-7])'; then
 echo 'unexpected floating point/SIMD instruction' >&2; exit 1
fi
echo 'libc: freestanding i386 compile and no-float/helper audit passed'

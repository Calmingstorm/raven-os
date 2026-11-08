#!/usr/bin/env python3
"""Find float (%f %e %g %a) and %n conversions in the string literals of preprocessed C, which RavenOS's printf
refuses. Reads `gcc -E -P` output on stdin; prints `<label>: <string>` for each offending string.

Strings are decoded as C does: adjacent literals joined, octal escapes of up to three digits, hex escapes taking
every following hex digit, \\u and \\U as UTF-8, and source text as UTF-8 bytes. A literal that can't be decoded
is reported too, so it can't slip through unchecked. --decode prints each decoded string as hex instead, for
checking the decoder against the compiler (tools/format-scan-test.sh).
"""
import re
import sys

LITERAL = rb'"(?:[^"\\\n]|\\.)*"'
RUN = re.compile(rb"(?:%s\s*)+" % LITERAL)
CONVERSION = re.compile(rb"%[-+ #0]*[0-9*]*(?:\.[0-9*]*)?(?:hh|h|ll|l|L|z|j|t)?[fFeEgGaAn]")
SIMPLE = {b"n": 10, b"t": 9, b"r": 13, b"a": 7, b"b": 8, b"f": 12, b"v": 11, b"e": 27,
          b"\\": 92, b"'": 39, b'"': 34, b"?": 63}
OCTAL, HEX = b"01234567", b"0123456789abcdefABCDEF"


def decode(body):
    """The bytes a C string literal's body stands for. Raises ValueError for what C wouldn't accept either."""
    out, i = bytearray(), 0
    while i < len(body):
        c = body[i:i + 1]
        i += 1
        if c != b"\\":
            out += c
            continue
        e = body[i:i + 1]
        i += 1
        if e and e in OCTAL:
            j = i - 1
            while i < len(body) and i - j < 3 and body[i:i + 1] in OCTAL:
                i += 1
            out.append(int(body[j:i], 8) & 0xFF)
        elif e == b"x":
            j = i
            while i < len(body) and body[i:i + 1] in HEX:
                i += 1
            if i == j:
                raise ValueError("\\x with no hex digits")
            value = int(body[j:i], 16)
            if value > 0xFF:
                raise ValueError("hex escape out of range")
            out.append(value)
        elif e in (b"u", b"U"):
            n = 4 if e == b"u" else 8
            digits = body[i:i + n]
            if len(digits) != n or any(d not in HEX for d in digits):
                raise ValueError("bad universal character name")
            out += chr(int(digits, 16)).encode("utf-8")
            i += n
        elif e in SIMPLE:
            out.append(SIMPLE[e])
        else:
            raise ValueError(f"unknown escape \\{e.decode('latin-1')}")
    return bytes(out)


def main():
    decode_only = "--decode" in sys.argv
    label = next((arg for arg in sys.argv[1:] if arg != "--decode"), "stdin")
    for run in RUN.finditer(sys.stdin.buffer.read()):
        literals = re.findall(LITERAL, run.group(0))
        try:
            text = b"".join(decode(literal[1:-1]) for literal in literals)
        except ValueError as error:
            print(f"{label}: undecodable literal ({error}): {run.group(0).decode('utf-8', 'replace')}")
            continue
        if decode_only:
            print(text.hex())
        elif CONVERSION.search(text.replace(b"%%", b"")):
            print(f"{label}: {text.decode('utf-8', 'replace')}")


if __name__ == "__main__":
    main()

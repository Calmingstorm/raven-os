#!/usr/bin/env python3
"""Find float (%f %e %g %a) and %n conversions in the strings compiled into object files: RavenOS's printf refuses
them. Usage: format-scan.py <object> ...; prints `<object>: <section>: <string>` for each offending string.

It reads the bytes the compiler emitted rather than lexing C, so escapes, adjacent literals, string prefixes and
character constants are already resolved exactly as C resolves them. Every NUL-terminated run in a string section
(.rodata.str*) is checked; in other .rodata and .data sections, which hold strings stored as character arrays among
other data, every stretch of printable text is, even when a nonprintable byte (an escape character, the end of a
struct field) runs straight into it.
"""
import os
import re
import subprocess
import sys
import tempfile

CONVERSION = re.compile(rb"%[-+ #0]*[0-9*]*(?:\.[0-9*]*)?(?:hh|h|ll|l|L|z|j|t)?[fFeEgGaAn]")
PRINTABLE = re.compile(rb"(?:[\t\n\r\x20-\x7e]|[\xc2-\xf4][\x80-\xbf]+)+")
SECTION = re.compile(r"\]\s+(\.(?:rodata|data)\S*)\s+(\S+)")


def data_sections(path):
    listing = subprocess.run(["readelf", "-SW", path], check=True, capture_output=True, text=True).stdout
    for line in listing.splitlines():
        match = SECTION.search(line)
        if match and match.group(2) != "NOBITS":
            yield match.group(1)


def section_bytes(path, section, scratch):
    dump = os.path.join(scratch, "section")
    subprocess.run(["objcopy", "-O", "binary", f"--only-section={section}", path, dump],
                   check=True, capture_output=True)
    with open(dump, "rb") as f:
        return f.read()


def main():
    with tempfile.TemporaryDirectory() as scratch:
        for path in sys.argv[1:]:
            for section in data_sections(path):
                strings_only = section.startswith(".rodata.str")
                for run in section_bytes(path, section, scratch).split(b"\0"):
                    texts = [run] if strings_only else [m.group(0) for m in PRINTABLE.finditer(run)]
                    for text in texts:
                        if CONVERSION.search(text.replace(b"%%", b"")):
                            print(f"{os.path.basename(path)}: {section}: {text.decode('utf-8', 'replace')}")


if __name__ == "__main__":
    main()

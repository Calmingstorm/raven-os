"""Check architectural error-code stack layouts, including optional exceptions.

QEMU/v86 guests exercise real #PF and #UD delivery; optional #CP/#VC/#SX are
structurally checked here, not claimed as executed on unsupported target CPUs.
"""
from pathlib import Path
import re

source = (Path(__file__).resolve().parents[1] / "kernel/isr.s").read_text()
stubs = [(kind, int(vector)) for kind, vector in
         re.findall(r"^ISR_(ERR|NOERR)\s+(\d+)\b", source, re.MULTILINE)]
assert sorted(vector for _, vector in stubs) == list(range(32))
errors = {8, 10, 11, 12, 13, 14, 17, 21, 29, 30}
assert {vector for kind, vector in stubs if kind == "ERR"} == errors
print("exception stubs: all 32 architectural error/no-error layouts passed")

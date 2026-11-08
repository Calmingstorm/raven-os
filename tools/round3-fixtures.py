#!/usr/bin/env python3
"""Synthetic IWAD, never Freedoom: exact transport lengths and final byte.

Sparse files keep the fixture cheap; this is NOT a playable or pinned WAD.
Two lumps exercise unaligned reads plus the final logical (partial) sector.
"""
import pathlib
import struct
import sys

out = pathlib.Path(sys.argv[1])
out.mkdir(parents=True, exist_ok=True)
logical = 28795076
image = 28835840  # 110 complete 256 KiB v86 chunks, 40,764 zero pad bytes.
for kind in ('valid', 'corrupt', 'short'):
    with (out / (kind + '.img')).open('wb') as f:
        f.truncate(image if kind != 'short' else 256 * 1024)
        f.seek(0)
        f.write(struct.pack('<4sII', b'IWAD', 2, 512))
        f.seek(512)
        extent = logical + 1 if kind == 'corrupt' else 1021
        f.write(struct.pack('<II8s', extent, 1024, b'TESTDATA'))
        f.write(struct.pack('<II8s', logical - 3, 3, b'FINAL'))
        if kind != 'short':
            f.seek(1021)
            f.write(bytes((i * 17 + 5) & 255 for i in range(1024)))
            f.seek(logical - 3)
            f.write(b'END')
for kind, sectors in (('master', 2048), ('small-master', 64)):
    with (out / (kind + '.img')).open('wb') as f:
        f.truncate(sectors * 512)
        f.seek(0)
        f.write(b'RAVEN master sentinel, never the WAD drive')
        f.seek(32 * 512)
        f.write(b'RAVENFS1')

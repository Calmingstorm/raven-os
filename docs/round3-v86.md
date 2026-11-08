# Round 3 synthetic runtime tests in v86

`tools/round3-v86-test.sh` builds the production-linked synthetic runtime guests
in a fresh, empty temporary kernel directory, generates synthetic disk fixtures
separately, and runs five machines in headless Chrome:

| Scenario | Master (`hda`) | Slave (`hdb`) | Expected WAD result |
| --- | --- | --- | --- |
| `valid` | 1 MiB sentinel | Valid synthetic IWAD | Read and clean up |
| `corrupt` | 1 MiB sentinel | Out-of-bounds lump extent | Refuse mount |
| `short` | 1 MiB sentinel | One 256 KiB chunk, truncated image | Refuse mount |
| `missing` | 1 MiB sentinel | Not attached | Refuse mount |
| `small-master` | 32 KiB sentinel | Valid synthetic IWAD | Read and clean up |

The small master verifies that slave bounds do not accidentally use master
capacity. An `hda` is always attached because the pinned v86 creates `hdb` only
when `hda` exists. None of these images contain an engine or playable game data.

## Running

From the repository root:

```sh
ROUND3_KEEP=1 bash tools/round3-v86-test.sh \
  --assets ../pr6-22919b7/web/dist \
  --playwright /path/to/node_modules/playwright-core \
  --report /tmp/round3-v86-report.json
```

Instead of Playwright, `--puppeteer /path/to/node_modules/puppeteer-core` is
supported. An installed `playwright-core` or `puppeteer-core` in the repository
or asset search paths is detected automatically. `--chrome FILE` overrides
`/usr/bin/google-chrome`. `CHROME_PATH`, `V86_ASSETS`, `PUPPETEER_PATH`, and
`ROUND3_REPORT` are also accepted. `--help` does not build anything.

Asset discovery tries `web/dist`, `web`, the sibling `pr6-22919b7/web/dist`, and
`build/console-v86`. The v86 JS and WASM must match the same pinned SHA-256 values
as the round 2 harness, v86 `0.5.473+g315b2b0`. BIOS hashes, input kernel hashes,
fixture hashes, serial output, browser debug-exit output, and transport requests
are included in the JSON report. No assets are downloaded or published.

`ROUND3_KEEP=1` retains the temporary build and fixture directories printed at
exit. Without it, they are deleted, including the default report under the
temporary kernel directory. Use `--report` to retain the report independently.

For already-built fixtures, the Node runner accepts `--kernels DIR --fixtures
DIR` instead of building them. The shell wrapper intentionally always builds.

## Assertions and boundaries

Each guest must print successful runtime, clock, libc, WAD or WAD-refusal, master
unchanged, and final assertion markers. It exercises runtime return, signed
exit status, abort, setjmp/longjmp recovery, allocation cleanup, and actual PIT
sleep. Valid WAD guests additionally exercise unaligned reads, short final
logical reads, EOF, and stdio cleanup. Any `ROUND3 FAIL` fails the host runner.

The only accepted browser exception is exactly one `HALT` paired with the
pinned v86 Multiboot debug-exit message `Test exited with code 0x10`. A timeout,
another exception, guest failure exit, or reboot is not success.

The master is a writable in-memory ArrayBuffer, not a read-only HTTP disk. The
guest reads and hashes every master sector before and after its tests. The host
also obtains the actual IDE master buffer via pinned v86 private fields,
verifies identity with the supplied writable ArrayBuffer, and compares its
SHA-256 with the original fixture. It does not merely rehash the source file.

The slave is configured with `async: true`, its exact physical `size`, and
`fixed_chunk_size: 256 * 1024`. The 28,835,840-byte image is exactly 110 such
chunks, including 40,764 padding bytes after the logical WAD. Every request
must return a complete chunk; a short final `206` is rejected by the harness.
The HTTP server implements byte ranges with exact 206 responses, Content-Range
and Content-Length; its bounded/open/suffix/clipped and invalid-range behavior
is self-tested before Chrome starts. The runner records every slave GET range,
requires 206 responses, requires total bytes below the complete image size in
valid scenarios, and requires fetching the final physical sector. The guest
checks the final logical bytes `END` without exposing physical padding.

Chrome uses a newly created temporary profile, headless, with no active desktop
session attachment. Only synthetic in-memory emulated disks are writable.

## Verified result

The final chunk-aligned run passed all five scenarios. Each valid scenario
transferred 524,288 of 28,835,840 slave bytes, exactly the first and final
256 KiB chunks, with no short responses. Corrupt and short scenarios each
transferred 262,144 bytes, and the missing-slave case transferred none. All
actual emulated master buffers retained their original SHA-256 hashes. This is a
synthetic runtime/transport test, not proof that a Doom engine or distribution
image boots or plays correctly.

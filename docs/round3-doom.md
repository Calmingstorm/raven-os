# Round 3: Doom

RavenOS runs Doom. The engine is doomgeneric (GPL-2.0, `third_party/doomgeneric/`), playing Freedoom Phase 1
(BSD-3-Clause) from a second disk. Type `doom` at the shell, or press Start Doom on the web page. This document
covers the port: graphics, keys, the data disk and the page. The runtime underneath (the freestanding libc, the
session boundary, the read-only WAD and the clock) is in `docs/round3-runtime.md`.

## What happens when you type `doom`

1. The runtime mounts the data disk (primary IDE slave) and checks the WAD's structure, then starts a session.
2. `DG_Init` asks whether 640x400x32 graphics can be entered (`gfx_available`), so a machine without a Bochs VBE
   display gets its error before anything loads. The screen stays in text mode.
3. Doom starts up and reads about 15 MB of the disk. Its startup log goes to the console, so the R_Init progress
   dots show while the data streams in.
4. On the first frame the port opens a gfx session (`gfx_begin`), the same checked mode switch the `gfx` command
   uses. From then on, console text is kept in the console's buffers and on serial, without touching VGA.
5. Each frame, doomgeneric scales Doom's 320x200 picture to 640x400. The port copies it into the framebuffer,
   which has its own stride.
6. Quitting (Quit Game, or F10 then Y) ends the session with `exit(0)`. A Doom error ends it with the error message.
   Either way the port's cleanup ends the gfx session, so the text console comes back with its font, palette and
   registers restored, and everything printed meanwhile shows. The shell prints how the session ended.

The engine initialises once per boot; a second `doom` is refused. On the web page, Restart gives a fresh machine.

## Keys

The kernel's keyboard driver queues every physical key, including the ones the shell has no use for (F1-F12,
Insert). Those arrive as `KEY_NONE` events, which the shell ignores. The port drains the queue into the key mapper
(`raven_doom_input.c`), which:

- maps by scancode, never by shifted character, so every release matches its press;
- merges left and right Shift, Ctrl and Alt into one Doom key each, pressed while either is down;
- makes X fire as well as Ctrl, because macOS keeps Ctrl with the arrow keys for switching desktops;
- ignores typematic repeats, and releases of keys pressed before Doom started;
- delivers at most 32 events per frame, because Doom's event ring holds 64 and doesn't check for overflow;
- recovers from overflow of its own 128-entry queue: the burst's order is given up and Doom is brought back to the
  keys actually held, releases first, so movement and fire can't stay stuck down.

## The data disk

`tools/build-wad.sh` downloads `freedoom-0.13.0.zip` once into `build/cache/` and checks it against Freedoom's
published SHA-256. It then pads `freedoom1.wad` (28,795,076 bytes) with zeros to 28,835,840 bytes: 110 chunks of
256 KiB.

The page reads the disk lazily through v86 in fixed 256 KiB chunks. v86's chunked reader mishandles a final chunk
that runs past the end of the file, which is why the disk is padded. The runtime never shows the padding: Doom sees
exactly the 28,795,076-byte WAD.

On the web the disk is `freedoom1-<first 16 hex digits of its SHA-256>.img`. v86 attaches a primary slave only
beside a primary master, so the page also gives it a blank 64-sector master. That is smaller than RavenFS needs, so
RavenFS keeps its RAM backend in the browser, as before, and nothing persists across reloads. Its name and size are in
`build.json`. nginx lets browsers keep it for good, and doesn't gzip it, because range requests need the raw bytes.
v86 reads one chunk while the page boots. The rest loads only when Doom runs: about 57 chunks before the first
frame, and a few more for each new level.

`tools/verify-web.mjs site` checks the deployed disk the way v86 reads it: a `206` with the exact `Content-Range`,
the exact bytes and no compression, for the first sector, a middle sector, the final sector, and whole chunks.

## Licences

RavenOS's own code is MIT (`LICENSE`). The kernel image includes GPL-2.0 code, so the image as a whole is
distributed under the GPL-2.0, and every web build ships the exact source tree it was built from
(`source/ravenos-source.tar.gz`), with the GPL-2.0 text and Freedoom's licence and credits. The page's footer links
to them. `third_party/doomgeneric/README.ravenos.md` lists every change to upstream files, and
`tools/doomgeneric-diff.sh` prints the exact diff against the pinned commit.

## Floating point

RavenOS has none. The engine is compiled with `-msoft-float -mno-sse -mno-mmx`. The few float paths in upstream are
compiled out, all of them for features RavenOS doesn't have:
- the float config variables;
- the mouse-speed diagnostic;
- the timedemo frame rate.

`tools/doom-audit.sh` enforces this:
- the engine imports exactly the agreed libc, runtime and bridge symbols, plus `__divdi3`, which Doom's `FixedDiv`
  needs, linked as libgcc's single object;
- the disassembly has no x87 or SIMD instruction;
- no format string uses a float conversion or `%n`.

## Tests

| Test | What it proves |
|---|---|
| `tools/doom-input-test.sh` | The key mapper: the map, release matching, merged modifiers, the per-frame limit, overflow recovery. |
| `tools/doom-host-test.sh` | The engine, the port and the mapper as a 32-bit Linux program, played by script on the real disk under the contract's file rules and a 10 ms clock: title, menu, new game, walking and stopping, firing and stopping, save refused, quit. Also: no graphics refuses before reading the disk, and a file that isn't a WAD ends the session with Doom's error. |
| `tools/doom-qemu-test.py` | The whole system in QEMU through the PS/2 keyboard: startup log, title, menu, new game, F10 and Y, the text console back pixel for pixel, a second launch refused, both disks unchanged. Also: no data disk, and a Cirrus display. |
| `tools/check-web.mjs` | In headless Chrome, from the page's Start Doom button, with each disk chunk delayed 400 ms like a real connection: the title, then the menu's Quit Game back to the shell. |
| `tools/doom-audit.sh` | The engine's imports, the libgcc object, no x87/SIMD, no float formats. |
| `tools/doomgeneric-diff.sh` | The vendored engine differs from the pinned commit only in the listed files. |
| `tools/vga-test.sh` | The console keeps text without touching VGA while graphics own the display. |

# Round-1 console regressions

Run `./tools/check.sh` for the kernel build, host tests, diskless QEMU console
integration and existing scratch-disk/USB ISO regressions. Dependencies are
listed in `.github/workflows/ci.yml`. The new tests require Python 3 and the
compiler's AddressSanitizer/UndefinedBehaviorSanitizer runtimes.

Focused runners:

* `./tools/keyboard-test.sh`: real set-1 parser and IRQ event queue with mocked
  input port. Make/break, E0 navigation and keypad Enter/slash, dual modifiers,
  Caps Lock, Ctrl+U, Pause/PrintScreen, unpaced 150/159-character make/break
  bursts (including per-character Shift), queue wraparound, exact drop counts
  and modifier recovery after overflow. The queue holds 1023 events; `stats`
  exposes a cumulative `keyboard_drops` counter (saturating at UINT32_MAX).
* `./tools/serial-editor-test.sh`: production shell serial rendering against an
  80x25 ANSI terminal model, including delayed autowrap and bottom scrolling.
  Exercises every cursor position on a 159-character line, shrink, whole-line
  clear, Enter positioning and prompt reset from all 25 origin rows.
* `./tools/vga-test.sh`: production VGA driver under ASan/UBSan. See
  `tests/vga-tests.md` for exhaustive input/scrollback coverage and API contract.
* `./tools/console-qemu-test.sh`: build first. Boots the production kernel under
  TCG with **no attached disk**, sends physical PS/2 keys through QMP, reads
  colored VRAM and pauses the guest to read actual CRTC position/visibility
  through qtest. Tests editing, 32-entry history/draft restoration, completion,
  help bounds, 256 colored historical rows and cursor/live-view transitions.
  Roughly one minute. Evidence is retained in its printed unique `/tmp`
  directory, including invocation, serial, latest VGA cells and cursor.

The QEMU console test checks all help topics against actual serial output and
VRAM: at most 79 columns and 23 lines including the new prompt. This leaves room
for the echoed help command. It also verifies `clear` renders exactly one prompt.

## Optional v86 guest integration

`./tools/console-v86-test.sh` is separate from `check.sh` because browser assets
and the web application are maintained on another branch. It tests the guest,
not web-page download, retry, focus, mobile layout or fullscreen behavior.

Build the kernel first. Supply v86 **0.5.473**, its matching `libv86.mjs` and
`v86.wasm`, SeaBIOS/VGA BIOS, Chrome, and `puppeteer-core`. The script does not
install dependencies or download assets. It accepts a flat `web/dist`, a `web`
directory with `node_modules/v86/build` and `vendor/bios`, or an isolated scratch
directory with `node_modules/v86/build` and `bios`. Do not commit dependencies,
BIOS blobs or generated reports to this kernel branch.

For example, when those scratch assets already exist:

```sh
./tools/build.sh
./tools/console-v86-test.sh --assets build/console-v86 \
  --puppeteer build/console-v86/node_modules/puppeteer-core
```

`--help` documents paths and environment overrides. A temporary HTTP listener
binds only to loopback; Chrome runs headless with its own temporary profile.
No active desktop session, deployed page or host disk is used. All browser and
emulator resources are closed after the test.

Assertions use actual `screen_adapter` text, color and hardware cursor update
hooks, not just serial strings. Direct Multiboot flags `0x40` are checked in the
rendered `boot` output, supplemented by a pinned-v86-specific loader metadata
check at `0x7c00`. Memory/heap initialization, PS/2 editing, history, Ctrl+U,
completion, wrapped input, unpaced 150/159-character paste with Enter (including
Shift make/break), zero drops for supported lines, help topics, colored
scrollback, cursor hide/restore and the guest's no-op Shift+Esc contract are
covered. The JSON report records the kernel SHA-256, snapshots, timings,
failures and limitations.

Overflow counting/recovery is tested directly in the host queue tests. v86 has
its own 1024-byte PS/2 FIFO: larger unpaced injections can lose scancodes there
before reaching the guest. The guest counter cannot account for upstream loss.

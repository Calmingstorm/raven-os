# VGA production-driver checks

Run from the repository root:

```sh
bash tools/vga-test.sh
```

This compiles and runs `tests/vga_test.c` with AddressSanitizer and
UndefinedBehaviorSanitizer. The harness includes the actual `kernel/vga.c`,
replacing only the physical VRAM address, port I/O, and serial sink. It then
compiles the normal hardware path with the freestanding i386 kernel flags.
Executables/objects live in a uniquely named temporary directory and are removed
on exit. No disk image, VM, service, working build directory, or real disk is
modified.

Assertions cover:

- Actual CRTC position registers after output, wrap, newline, carriage return,
  backspace, input edits, and clear.
- Start/end scan-line shape preservation and cursor disable bit while browsing.
- Every historical viewport offset through a rolled-over 256-line ring, including
  per-cell text and colors, exact oldest-line eviction, saturation at either end,
  `INT_MIN`/`INT_MAX` deltas, and background output while browsing.
- Deterministic history/viewport reset and visible cursor on clear.
- Bottom-row input insert/delete crossing a wrap, two-row scrolling for a
  159-character input after the seven-column prompt, shrink erasure, prompt color
  preservation, middle editing cursor, end-of-stream placement, and newline after
  editing ends.
- Exhaustive lengths 0..159..0 at every possible prompt row, plus oversized length
  and cursor clamps, null text, and clear during input.
- Output bytes remain mirrored to serial, while input redraw neither emits serial
  bytes nor duplicates serial echo.

## Integration API

Add these declarations to the console header:

```c
void vga_scrollback(int lines);
void vga_live(void);
void vga_input_begin(void);
void vga_input_redraw(const char *text, uint32_t len, uint32_t cursor);
void vga_input_end(void);
```

`vga_scrollback` accepts positive lines to browse backward, negative to browse
forward. History is exactly 256 additional colored rows beyond the live 25-row
screen, statically allocated and independent of the heap. Browsing hides the
cursor. `vga_live` returns to the live surface without changing stream position or
the editing cursor. `vga_clear` resets all history and any active input anchor.

Call `vga_input_begin` **after** emitting the prompt. Call `vga_live` before typing
or editing actions, then `vga_input_redraw` with the entire input and the desired
cursor offset. Length is limited to 159 bytes and cursor to that length. Redraw is
VGA-only: the shell owns serial echo. Redraw leaves stream position at the input's
end but puts the hardware cursor at the editing offset. Call `vga_input_end`
before emitting the newline, so the hardware cursor returns to the stream end.
Ordinary `vga_putc` output also ends the active input anchor.

Already completed screen scrolls are not undone when an input shrinks. The prompt
stays at its adjusted screen row, stale wrapped characters are erased, and the
stream end moves back to the shortened text. This avoids removing unrelated
console history or silently shifting output down again.

## Limits

These are host hardware-boundary tests, not a QEMU or v86 visual acceptance run.
They prove driver state, VRAM contents, and CRTC register writes, not emulator
presentation or browser focus behavior. Editing assumes synchronous shell output
and one active input line. The shell must begin a new anchor after asynchronous
output or clear. The hardware path assumes the existing 80x25 color VGA text mode
at `0xB8000`, CRTC `0x3D4/0x3D5`, and firmware cursor scan-line shape.

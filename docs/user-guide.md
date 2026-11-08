# RavenOS user guide

Boot the ISO in QEMU:

```bash
./tools/run-qemu.sh
```

Run with a raw IDE dev disk:

```bash
./tools/run-qemu-disk.sh
```

Run validation:

```bash
./tools/check.sh
```

Run the interactive serial shell validation directly:

```bash
./tools/serial-command-test.sh
```

Useful shell commands:

- `help` - screen-sized grouped command overview
- `help system|memory|files|disk|net|runtime|controls` - topic usage
- `serial` - serial console status
- `timeline` - recent wyrd events
- `health` - operator health report
- `fs` - list RAM files
- `edit /home/demo hello` - write a RAM file
- `cat /home/demo` - read a RAM or RavenFS file
- `save /home/demo` - persist RAM file into RavenFS block image
- `rm /home/demo` - remove RAM file
- `load /home/demo` - load RavenFS file back into RAM
- `disk` - show RavenFS block image
- `format` - reset RavenFS
- `install` - show install plan
- `install-run` - write base system to RavenFS
- `pkgs` - package catalog
- `pkg-install games` - mark a package installed
- `exec`, `load /bin/hello.rvn`, `run hello`, `run clock`, `run writer`, `run netdemo` - executable registry and supervised demo programs
- `rings` - inspect GDT/TSS/ring architecture
- `ata` - inspect ATA identify probe
- `ata-write`, `ata-read` - write/read LBA 20 on the QEMU dev disk
- `procps`, `ps-spawn` - inspect and create process metadata
- `net`, `sock`, `send 1 hello`, `ping` - loopback network/socket tools
- `tasks`, `spawn`, `kill <id>` - task table operations
- `metrics`, `proc`, `manifest`, `services`, `security` - diagnostics

## Console editing

The PS/2 keyboard works in QEMU and v86. Input is limited to 159 characters,
including arguments. The hardware cursor follows the insertion point, including
when the input wraps across rows.

- Left/Right and Home/End move within the line; typing inserts at the cursor.
- Backspace deletes before the cursor; Delete removes the character under it.
- **Ctrl+U clears the entire input line**, even with the cursor in the middle.
  Web command buttons should send Ctrl+U before their complete command.
- Up/Down traverse the last 32 nonempty commands. Consecutive duplicates are
  coalesced. The first Up saves the unfinished draft and its cursor position;
  returning past the newest entry with Down restores both. Editing a recalled
  command does not change the stored entry. History is volatile.
- Tab completes command names only, at the end of the first token. It extends a
  shared prefix or prints matching names when ambiguous. It does not complete
  arguments or paths.
- Shift+PageUp / Shift+PageDown browse output in 24-line steps. Up to 256
  historical rows retain their text and colors independently of the heap. The
  cursor is hidden while browsing. Typing or editing returns to live output.
- Shift+Esc has **no guest action**; the web host can reserve it to release focus.
- `clear` resets output and scrollback and renders the home screen with one
  prompt. It does not erase command history.

The serial shell still accepts plain command lines, Backspace, Tab and byte
`0x15` (Ctrl+U). Editor redraws use ANSI terminal sequences on serial; PS/2
navigation is not an ANSI serial-input parser.

## Memory diagnostics and safe testing

`boot` reports the validated Multiboot memory source; `mem` reports the bounded
physical allocator. A supplied memory map takes precedence over basic memory
totals. Reserved regions, holes, low memory, the kernel and retained boot data
(the information structure, map, modules and boot strings) are not allocatable.
Optional metadata not consumed by RavenOS is not retained. An invalid supplied
map fails closed rather than inventing contiguous RAM. Only a missing map
permits the basic-memory fallback.

The allocator still caps its address space at 128 MiB. This does **not** extend
paging beyond the first 4 MiB or grow the prototype one-page heap.

Only attach disposable disk images to these tests. RavenFS may format an
unrecognized attached volume during boot. The console regression test boots
without a disk and examines actual VGA text cells and hardware cursor state.
See `tests/console-tests.md` and `tools/memory-regression.md` for focused runners,
coverage, limits and the optional isolated v86 guest test.

RavenOS is not POSIX. The shell is an operator console over the wyrd log and kernel objects, not a Unix cosplay terminal. Good. The world has enough beige mistakes.

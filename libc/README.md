# Raven Doom freestanding subset

This is the bounded runtime libc for the pinned consumer **doomgeneric
`e257a09527062bfb986b6e5e0780839b6cbd5b71`**. It is not a hosted ISO C/POSIX
implementation. Header presence is not evidence that an engine feature works.
The runtime is main-loop-only, single-context, and must never run from IRQs.

## Build and ownership

Compile every `libc/*.c` with the kernel's freestanding i386 flags, additionally
`-msoft-float -mno-sse -mno-mmx`, `-nostdinc`, the compiler internal include
directory, `-Ilibc/include -Ikernel`. No host headers or host libc link are used.
The implementation requires no libgcc helpers. Integer 64-bit formatting and
scanning use binary division by a small base, not `__udivdi3`.

`kernel/string.c` remains the sole provider of `memcpy`, `memset`, `strlen`,
`strcmp`, `strncmp`. The remaining string routines live here. The malloc family
forwards exclusively to `raven_doom_malloc/calloc/realloc/free`; it does not
interpose on kernel kmalloc/kfree. `strdup` uses that same session domain.
`exit`, `abort`, and failed enabled assertions use the nonreturning runtime
boundary. An assertion diagnostic reports expression/file/line to serial first.
`NDEBUG` follows C semantics and disables evaluation of assertions.

The parent runtime calls `raven_libc_reset_streams()` **before**
`raven_wad_unmount()`, after port cleanup. It closes active libc handles, clears
slots and console error state, and allocates nothing. Streams are fixed slots;
using a FILE pointer after close/reset is invalid, including after slot reuse.

## Implemented behavior

* ASCII ctype recognizes EOF and unsigned bytes without locale or signed-char
  indexing. Callers still cast potentially negative plain chars to unsigned char.
* `atoi` parses optional ASCII whitespace/sign and decimal digits. As a defined
  extension, range errors clamp to INT_MIN/MAX and set ERANGE. `abs(INT_MIN)`
  retains ISO C's unrepresentable-result undefined behavior; do not call it.
* No environment exists: `getenv` returns NULL.
* `fopen` accepts exactly `r` and `rb` and the two WAD aliases. Unknown read paths
  report ENOENT. All other modes report EROFS, never fake write success.
* Each of eight streams has independent cursor/EOF/error. `fread` checks
  `size*count`, returns complete elements, and advances over a partial last
  element. Partial device errors preserve transferred-byte position and set
  error rather than EOF. Exact-end reads set EOF only on a subsequent read;
  successful seeks clear EOF, not error. `clearerr` clears both.
* Checked signed `fseek` supports SET/CUR/END and positions through LONG_MAX,
  including beyond EOF. Failure does not change position. Negative targets are
  EINVAL; arithmetic overflow is EOVERFLOW.
* stdout/stderr write synchronously through the kernel console (`vga_putc`), which mirrors every byte to
  serial. While a graphics session owns the display, the console keeps the text without touching VGA registers,
  VRAM or the framebuffer, and shows it when text mode returns.
  `fflush(NULL)` is valid because there are no buffered bytes. WAD writes,
  remove, rename and mkdir fail EROFS. Invalid stream operations fail EBADF.
* `printf` family implements d/i/u/o/x/X, c/s/p/percent, `-+ #0`, decimal width,
  precision and `*`, and hh/h/l/ll/z integer lengths. Pointer output is `0x` plus
  hex digits (NULL is `0x0`). String precision bounds input reads; snprintf
  reports the full would-have-written length and terminates when capacity > 0.
  NULL destination is valid only for zero capacity. Return-count overflow is
  EOVERFLOW. There is no heap allocation or fixed output truncation limit.
* `sscanf` implements d/i/u/o/x/X, c/s, widths, optional sign and 0x prefix,
  ASCII whitespace/literal matching/percent and assignment suppression. Integer
  lengths hh/h/l/ll/z have destination-width range checking. Overflow sets
  ERANGE, leaves that destination unchanged, and returns prior assignments.
  Negative unsigned inputs apply modulo destination width when the magnitude
  is representable. Bare signs or bare 0x are matching failures. `%c` requires
  its exact width and never partially writes on short input. `%s` terminates;
  callers must bound width to their actual destination capacity minus one.
* Unsupported/malformed format directives return -1/ENOSYS. Both printf and
  scanf validate the **entire format before consuming variadic arguments** or
  writing formatted output/assigned destinations. There are no floats, `%n`,
  wide chars/strings, positional arguments or scanf scansets. Numeric printf
  width/precision literal overflow is EOVERFLOW. Null format/source arguments
  remain invalid C API inputs (snprintf diagnoses a null format as EINVAL).

No descriptor I/O/process API is declared by fcntl.h/unistd.h: these are empty
compatibility includes. No atexit, qsort, rand, stat or math library is provided.

## Explicit consumer integration gate: NO atof in this initial subset

The proposed contract listed atof. This implementation deliberately removes it
from stdlib.h and the link surface rather than shipping a zero-returning stub,
assuming x87 state, or silently adding soft-float/compiler-runtime dependencies.
**The unmodified pinned engine is not compatible and has NOT been linked here.**

Inspected upstream evidence:

* `m_config.c:104` defines CONFIG_VARIABLE_FLOAT; table entries at 773 and 1106
  are mouse_acceleration and libsamplerate_scale. They are not imaginary types.
* `m_config.c:1698` saves DEFAULT_FLOAT using `%f`; 1765-1766 assigns via atof;
  2025 defines `M_GetFloatVariable`, which returns a float. Disabling only file
  loading with ORIGCODE is insufficient: the setter remains compiled.
* `i_sdlmusic.c:161` also calls atof for music timing. SDL/Allegro audio objects
  are outside the agreed no-audio retained object set and must stay excluded.
* Float globals/uses also occur in i_sound.c, i_video.c, v_video.c and audio
  backends. Removing atof alone does not establish a float-free engine.

Before the separate engine integration may pass:

1. Pin and record the exact retained object list and feature macros. Keep host
   video/audio/process backends excluded, not just unreachable at runtime.
2. Deliberately remove DEFAULT_FLOAT, CONFIG_VARIABLE_FLOAT and its two entries,
   float case arms in config save/set and M_GetFloatVariable (including header
   declaration). Remove/update all bindings and callers for those variables.
   Do not replace these with pretend integer/zero conversions.
3. Remove unused float mouse-speed diagnostic/config/audio branches and their
   float globals, or provide a separately reviewed explicit arithmetic/runtime
   plan if any live feature genuinely requires them. Remove unused math.h
   includes; do not add libm for disabled table-generation code.
4. Disable config persistence and save/load/menu/hotkey paths intentionally.
   Sending them to EROFS and triggering I_Error is not a functional adaptation.
5. Compile **all** retained engine objects under the no-FPU flags, with implicit
   function declarations fatal. Audit undefined symbols and final linker map,
   and disassemble for x87/SSE/MMX. No atof, libm, float compiler helper or
   unexpected integer helper may remain. Audit dynamic format strings too.
6. Exercise the patched consumer and acceptance cases before claiming engine
   support. Runtime mocks/native libc tests do not establish real Doom success.

Reproducible source-only audit against the pinned tree (no engine link/publish)
found `m_config.o` importing `atof` and `__floatsisf` under -msoft-float; this is
an expected **failed integration gate**, not a supported link. `w_file_stdc.o`,
`z_zone.o`, `m_argv.o`, `memio.o` compile against these headers with only expected
libc/engine imports. `m_misc.c` needs EISDIR in its historical FileExists branch;
the constant is supplied, without inventing directory filesystem behavior.

## Tests

Run `tools/libc-test.sh`. It builds all shipped libc sources plus the actual
kernel/string.c with prefixed custom public symbols, avoiding interception of
glibc/ASan startup routines. The host bridge alone includes host system headers
and owns host allocation/setjmp. ASan+UBSan cover string overlap/bounds, all
ASCII bytes, conversion limits, allocation failure, termination forwarding,
format boundaries and 2,000 seeded 64-bit differential comparisons to host
vsnprintf, 3,000 seeded base roundtrips plus signed/unsigned scanf extrema,
unsupported-format prevalidation, independent WAD cursors, EOF/error distinction,
partial I/O failure, stream limits/reset and EROFS operations.

The script also compiles every libc source with production i386 flags, combines
the objects and enforces an exact undefined-symbol allowlist (only kernel
strings, runtime alloc/termination, WAD operations and serial output). It rejects
unexpected compiler helpers and disassembled x87/SIMD. This is not the final
engine map: the engine integration gate above remains mandatory.

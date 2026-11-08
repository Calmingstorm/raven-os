# Raven clock regression tests

Run `bash tools/clock-test.sh`. Requires GCC/binutils, Python 3 and
`qemu-system-i386`; no 32-bit host libc is required. The guest is disposable,
diskless, and uses TCG rather than the active desktop session.

## Coverage and evidence

- The host test includes the production `timer.c` and `raven_clock.c`, replacing
  only the privileged-instruction helpers. It exercises the actual unsigned
  multiplication at tick rollover and millisecond rollover, ordinary rounding,
  zero-duration no-op, spurious wakeups, an IRQ before CLI, a pending IRQ at the
  STI/HLT boundary, enabled-IF return and disabled-IF programmer-fault handling.
- Accelerated tick advances exercise intervals just below/above `INT32_MAX`
  and `UINT32_MAX`. This is algorithmic full-range evidence, not a 49.7-day
  wall-clock test. Intervals are bounded below half the modulo range and split
  on multiples of 10 ms so the final rounding cannot overflow the duration.
- An optimized production i386 build is disassembled to assert adjacent
  `sti; hlt`, CLI and UD2 instructions. Its undefined-symbol list must not
  contain shell or keyboard calls. The host seam does not by itself establish
  physical interrupt-shadow behavior; that property is architectural.
- The diskless multiboot QEMU guest uses production clock code and the actual
  PIT initialization/IRQ handler logic at 100 Hz. It crosses the tick and ms
  rollover boundaries, repeatedly sleeps using real HLT, and verifies enabled
  IF on return. Its IRQ0 timing permits a pending IRQ before the initial sample,
  rather than assuming an unrealistically exact number of guest wakeups.

`timer_ticks() * 10u` remains continuous modulo 2^32 when `timer_ticks()` itself
wraps. At the boundary, `(0 - UINT32_MAX) * 10 == 10 (mod 2^32)`. No second
counter or clock reset is needed. The IRQ-written tick variable is volatile
and naturally aligned; reads are single-word and atomic on the uniprocessor.

The API requires IF enabled and main-loop context for positive waits. A
positive wait with IF disabled executes UD2 rather than silently enabling
interrupts or hanging forever. Zero remains a no-op regardless of IF. No
test claims to detect every possible violation of the main-loop precondition.

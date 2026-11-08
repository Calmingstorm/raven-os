#ifndef RAVEN_CLOCK_H
#define RAVEN_CLOCK_H

#include <stdint.h>

/* PIT stays at 100 Hz: milliseconds, 10 ms resolution, modulo 2^32.
 * Atomic on our uniprocessor. Compare elapsed time by unsigned subtraction.
 * This clock is never reset for a Doom session.
 */
uint32_t raven_clock_ms(void);

/* Main-loop only, IF enabled, never from an IRQ/exception. Zero is a no-op.
 * Full uint32_t durations are supported, rounded up to timer resolution.
 * HLT waits do not service the shell or consume keyboard events.
 * A positive call with IF disabled is a programming error (invalid opcode).
 */
void raven_sleep_ms(uint32_t milliseconds);

#endif

#include "kernel.h"
#include "raven_clock.h"

/* Private instruction seam for deterministic host tests. Production always
 * uses the actual privileged instructions below.
 */
#ifndef RAVEN_CLOCK_TEST
static uint32_t clock_flags(void) {
    uint32_t flags;
    __asm__ volatile ("pushfl; popl %0" : "=r" (flags) :: "memory");
    return flags;
}

static void clock_cli(void) {
    __asm__ volatile ("cli" ::: "memory");
}

static void clock_sti(void) {
    __asm__ volatile ("sti" ::: "memory");
}

static void clock_sti_hlt(void) {
    /* STI's interrupt shadow prevents delivery between STI and HLT. A pending
     * IRQ therefore wakes HLT instead of being consumed just before it.
     */
    __asm__ volatile ("sti; hlt" ::: "memory");
}

static void clock_bad_context(void) {
    __asm__ volatile ("ud2");
}
#endif

uint32_t raven_clock_ms(void) {
    /* A single aligned 32-bit timer read is atomic on i386. Multiplication
     * is unsigned, so even the tick-counter wrap remains continuous:
     * (0 - UINT32_MAX) * 10 == 10 (mod 2^32). No separate ms epoch is needed.
     */
    return timer_ticks() * UINT32_C(10);
}

void raven_sleep_ms(uint32_t milliseconds) {
    if (milliseconds == 0) return;
    if (!(clock_flags() & (UINT32_C(1) << 9))) {
        clock_bad_context();
        return;
    }

    /* Below half the modulo range and a multiple of 10 ms. Splitting avoids
     * signed-deadline limits and lets UINT32_MAX round up correctly without
     * overflowing the requested duration itself.
     */
    const uint32_t max_interval = UINT32_C(2147483640);
    while (milliseconds != 0) {
        uint32_t interval = milliseconds > max_interval
                          ? max_interval : milliseconds;
        uint32_t start = raven_clock_ms();
        for (;;) {
            clock_cli();
            if ((uint32_t)(raven_clock_ms() - start) >= interval) {
                clock_sti();
                break;
            }
            clock_sti_hlt();
        }
        milliseconds -= interval;
    }
}

/* Exercise the actual timer and clock sources, replacing only privileged
 * instruction helpers. No wall-clock wait or 32-bit host libc is needed.
 */
#include <assert.h>
#include <stdio.h>
#include <stdint.h>

#include "../kernel/timer.c"

static unsigned io_writes, init_events;
void outb(uint16_t port, uint8_t byte) {
    (void)port;
    (void)byte;
    ++io_writes;
}
void wyrd_append(const char *actor, const char *verb, const char *object,
                 const char *result, const char *note) {
    (void)actor; (void)verb; (void)object; (void)result; (void)note;
    ++init_events;
}

static int if_enabled, bad_context;
static uint64_t halts, elapsed_ticks;
static unsigned cli_count, sti_count, step_index;
static uint32_t steps[4];
static int accelerated, spurious, inject_before_cli, pending_irq;

static uint32_t clock_flags(void) { return if_enabled ? 0x200u : 0; }
static void clock_cli(void) {
    assert(if_enabled);
    /* IRQ arrives just before CLI: its time must be seen by the check. */
    if (inject_before_cli) {
        timer_on_irq();
        ++elapsed_ticks;
        inject_before_cli = 0;
    }
    if_enabled = 0;
    ++cli_count;
}
static void clock_sti(void) {
    assert(!if_enabled);
    if_enabled = 1;
    ++sti_count;
}
static void clock_sti_hlt(void) {
    assert(!if_enabled);
    if_enabled = 1;
    ++halts;
    if (spurious) { --spurious; return; }
    /* A pending IRQ between check and HLT is delivered after the atomic
     * STI/HLT pair, not lost between the two instructions.
     */
    if (pending_irq) pending_irq = 0;
    uint32_t step = accelerated ? steps[step_index++] : 1;
    assert(step != 0 && step_index <= 4);
    pit_ticks += step;
    elapsed_ticks += step;
}
static void clock_bad_context(void) { ++bad_context; }

#define RAVEN_CLOCK_TEST
#include "../kernel/raven_clock.c"

static void reset(uint32_t tick) {
    pit_ticks = tick;
    if_enabled = 1;
    bad_context = 0;
    halts = elapsed_ticks = 0;
    cli_count = sti_count = step_index = 0;
    accelerated = spurious = inject_before_cli = pending_irq = 0;
}

int main(void) {
    timer_init(100);
    assert(io_writes == 3 && init_events == 1);
    assert(timer_ticks() == 0);
    reset(UINT32_MAX);
    uint32_t before = raven_clock_ms();
    timer_on_irq();
    assert(timer_ticks() == 0 && raven_clock_ms() == 0);
    assert((uint32_t)(raven_clock_ms() - before) == 10);

    /* Millisecond wrap occurs before tick wrap, without resetting ticks. */
    reset(UINT32_C(429496729));
    before = raven_clock_ms();
    timer_on_irq();
    assert(before == UINT32_C(4294967290) && raven_clock_ms() == 4);
    assert((uint32_t)(raven_clock_ms() - before) == 10);

    const uint32_t durations[] = {0, 1, 9, 10, 11, 19, 20, 21, 1001};
    const uint32_t starts[] = {0, UINT32_C(429496729), UINT32_MAX - 1};
    for (unsigned i = 0; i < sizeof(starts) / sizeof(starts[0]); ++i) {
        for (unsigned j = 0; j < sizeof(durations) / sizeof(durations[0]); ++j) {
            reset(starts[i]);
            raven_sleep_ms(durations[j]);
            uint32_t ticks = (durations[j] + 9) / 10;
            assert(elapsed_ticks == ticks && halts == ticks);
            assert(if_enabled && !bad_context);
        }
    }

    reset(0);
    spurious = 5;
    pending_irq = 1;
    raven_sleep_ms(11);
    assert(halts == 7 && elapsed_ticks == 2 && !pending_irq && if_enabled);

    reset(0);
    inject_before_cli = 1;
    raven_sleep_ms(1);
    assert(halts == 0 && elapsed_ticks == 1 && if_enabled);

    /* Full unsigned range in three intervals, with accelerated IRQ steps.
     * These are clock progression simulations, not 49.7-day realtime tests.
     */
    const uint32_t large[] = {UINT32_C(2147483640), UINT32_C(2147483641),
                             UINT32_C(2147483648), UINT32_MAX};
    for (unsigned i = 0; i < sizeof(large) / sizeof(large[0]); ++i) {
        reset(UINT32_MAX - 2);
        accelerated = 1;
        uint32_t remaining = large[i];
        unsigned count = 0;
        while (remaining) {
            uint32_t part = remaining > UINT32_C(2147483640)
                          ? UINT32_C(2147483640) : remaining;
            steps[count++] = (part + 9) / 10;
            remaining -= part;
        }
        raven_sleep_ms(large[i]);
        assert(elapsed_ticks * 10 == ((uint64_t)large[i] + 9) / 10 * 10);
        assert(halts == count && if_enabled);
    }

    reset(123);
    if_enabled = 0;
    raven_sleep_ms(0);
    assert(!if_enabled && bad_context == 0 && cli_count == 0 && halts == 0);
    raven_sleep_ms(1);
    assert(!if_enabled && bad_context == 1 && cli_count == 0 && halts == 0);
    puts("clock: modulo tick/ms rollover, rounding, spurious/pending IRQs, full uint32 durations, IF policy PASS");
    return 0;
}

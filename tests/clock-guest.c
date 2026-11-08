/* Disposable, diskless QEMU guest. Use the production PIT source so the
 * rollover cases can seed its private tick count without a public reset API.
 */
#include "../kernel/kernel.h"
#include "../kernel/raven_clock.h"
#include "../kernel/timer.c"

static IdtEntry gates[256];
extern void clock_guest_irq(void);
static volatile uint32_t irq_count;

void wyrd_append(const char *a, const char *v, const char *o,
                 const char *r, const char *n) {
    (void)a; (void)v; (void)o; (void)r; (void)n;
}

static void message(const char *s) {
    while (*s) {
        while (!(inb(0x3fd) & 0x20)) {}
        outb(0x3f8, (uint8_t)*s++);
    }
}

static void finish(int passed) {
    message(passed ? "clock: QEMU real PIT/IRQ HLT, IF, tick and ms rollover PASS\n"
                   : "clock: QEMU FAIL\n");
    outb(0xf4, passed ? 0x10 : 0x11);
    for (;;) __asm__ volatile ("cli; hlt");
}

static uint32_t flags(void) {
    uint32_t result;
    __asm__ volatile ("pushfl; popl %0" : "=r"(result));
    return result;
}

void clock_guest_on_irq(void) {
    timer_on_irq();
    ++irq_count;
    outb(0x20, 0x20);
}

static void init_irq(void) {
    uint16_t cs;
    __asm__ volatile ("mov %%cs,%0" : "=r"(cs));
    uintptr_t address = (uintptr_t)clock_guest_irq;
    gates[32] = (IdtEntry){(uint16_t)address, cs, 0, 0x8e,
                          (uint16_t)(address >> 16)};
    IdtPointer descriptor = {sizeof(gates) - 1, (uint32_t)(uintptr_t)gates};
    __asm__ volatile ("lidt %0" :: "m"(descriptor) : "memory");
    outb(0x20, 0x11); outb(0xa0, 0x11);
    outb(0x21, 0x20); outb(0xa1, 0x28);
    outb(0x21, 4);    outb(0xa1, 2);
    outb(0x21, 1);    outb(0xa1, 1);
    outb(0x21, 0xfe); outb(0xa1, 0xff);
    timer_init(100);
}

void kernel_main(uint32_t magic, uint32_t info) {
    (void)magic; (void)info;
    __asm__ volatile ("cli" ::: "memory");
    outb(0x3f9, 0); outb(0x3fb, 0x80); outb(0x3f8, 3);
    outb(0x3f9, 0); outb(0x3fb, 3); outb(0x3fa, 0xc7);
    raven_sleep_ms(0);
    if (flags() & 0x200) finish(0);
    init_irq();

    /* Cross the timer's uint32_t rollover through actual IRQ0 updates. */
    pit_ticks = UINT32_MAX - 1;
    uint32_t start = raven_clock_ms();
    __asm__ volatile ("sti" ::: "memory");
    raven_sleep_ms(31);
    __asm__ volatile ("cli" ::: "memory");
    /* IRQ0 may already be pending when STI enables it before the function's
     * start sample. Permit that scheduling delay, not an early return.
     */
    if (pit_ticks > 8 || (uint32_t)(raven_clock_ms() - start) < 40 ||
        (uint32_t)(raven_clock_ms() - start) > 100) {
        message("clock: tick rollover assertion\n");
        finish(0);
    }

    /* Cross the millisecond modulo boundary before tick-counter rollover. */
    pit_ticks = UINT32_C(429496729);
    start = raven_clock_ms();
    __asm__ volatile ("sti" ::: "memory");
    raven_sleep_ms(11);
    __asm__ volatile ("cli" ::: "memory");
    if (raven_clock_ms() > 94 || (uint32_t)(raven_clock_ms() - start) < 20 ||
        (uint32_t)(raven_clock_ms() - start) > 100) {
        message("clock: ms rollover assertion\n");
        finish(0);
    }

    __asm__ volatile ("sti" ::: "memory");
    for (unsigned i = 0; i < 64; ++i) {
        start = raven_clock_ms();
        uint32_t count = irq_count;
        raven_sleep_ms(i % 3 == 0 ? 11 : 1);
        if (!(flags() & 0x200) || irq_count <= count ||
            (uint32_t)(raven_clock_ms() - start) < (i % 3 == 0 ? 20u : 10u)) {
            message("clock: repeated wait/IF assertion\n");
            finish(0);
        }
    }
    finish(1);
}

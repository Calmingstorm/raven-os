#include "kernel.h"

static uint32_t pit_ticks;

void timer_init(uint32_t frequency) {
    pit_ticks = 0;
    uint32_t divisor = 1193180 / frequency;
    outb(0x43, 0x36);
    outb(0x40, (uint8_t)(divisor & 0xFF));
    outb(0x40, (uint8_t)((divisor >> 8) & 0xFF));
    wyrd_append("driver.pit", "init", "irq0", "ok", "system tick at 100Hz");
}

void timer_on_irq(void) {
    pit_ticks++;
}

uint32_t timer_ticks(void) {
    return pit_ticks;
}

void timer_wait(uint32_t ticks) {
    uint32_t end = pit_ticks + ticks;
    while (pit_ticks < end) __asm__ volatile ("hlt");
}

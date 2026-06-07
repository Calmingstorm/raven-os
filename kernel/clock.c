#include "kernel.h"

static uint32_t boot_epoch;

void clock_init(void) {
    boot_epoch = 0;
    wyrd_append("clock", "init", "monotonic", "ok", "PIT-backed uptime clock");
}

void clock_render(void) {
    uint32_t ticks = timer_ticks();
    uint32_t seconds = ticks / 100;
    vga_set_color(COLOR_LIGHT_CYAN, COLOR_BLACK);
    vga_write("clock\n");
    vga_set_color(COLOR_LIGHT_GREY, COLOR_BLACK);
    vga_write("  boot_epoch="); vga_write_dec(boot_epoch); vga_putc('\n');
    vga_write("  ticks="); vga_write_dec(ticks); vga_putc('\n');
    vga_write("  uptime_seconds="); vga_write_dec(seconds); vga_putc('\n');
}

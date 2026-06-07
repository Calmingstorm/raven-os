#include "kernel.h"

void health_render(void) {
    vga_set_color(COLOR_LIGHT_CYAN, COLOR_BLACK);
    vga_write("health check\n");
    vga_set_color(COLOR_LIGHT_GREY, COLOR_BLACK);
    vga_write("  timer: "); vga_write(timer_ticks() ? "ok" : "warming"); vga_putc('\n');
    vga_write("  wyrd events: "); vga_write_dec(wyrd_count()); vga_write(" ok\n");
    vga_write("  pmm free frames: "); vga_write_dec(pmm_free_frames()); vga_write(pmm_free_frames() ? " ok\n" : " fail\n");
    vga_write("  ramfs motd: "); vga_write(ramfs_read("/etc/motd") ? "ok" : "missing"); vga_putc('\n');
    vga_write("  capability panic.trigger/shell: "); vga_write(cap_check("panic.trigger", "shell") ? "unexpected grant" : "denied ok"); vga_putc('\n');
    wyrd_append("health", "check", "system", "ok", "operator health report rendered");
}

#include "kernel.h"

void health_render(void) {
    uint32_t free_frames = pmm_free_frames();
    const char *motd = ramfs_read("/etc/motd");
    const char *persist = storage_load_file("/persist/readme");
    vga_set_color(COLOR_LIGHT_CYAN, COLOR_BLACK);
    vga_write("health check\n");
    vga_set_color(COLOR_LIGHT_GREY, COLOR_BLACK);
    vga_write("  timer: "); vga_write(timer_ticks() ? "ok" : "warming"); vga_putc('\n');
    vga_write("  wyrd events: "); vga_write_dec(wyrd_count()); vga_write(" ok\n");
    vga_write("  pmm free frames: "); vga_write_dec(free_frames); vga_write(free_frames ? " ok\n" : " fail\n");
    vga_write("  ramfs motd: "); vga_write(motd ? "ok" : "missing"); vga_putc('\n');
    vga_write("  ravenfs load: "); vga_write(persist ? "ok" : "missing"); vga_putc('\n');
    vga_write("  ata probe: "); vga_write(ata_present_flag() ? "present" : "not detected"); vga_putc('\n');
    vga_write("  network loopback: "); net_ping_self();
    vga_write("  capability panic.trigger/shell: "); vga_write(cap_check("panic.trigger", "shell") ? "unexpected grant" : "denied ok"); vga_putc('\n');
    wyrd_append("health", "check", "system", "ok", "operator health report rendered");
}

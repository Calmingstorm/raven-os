#include "kernel.h"

void procfs_init(void) {
    ramfs_write("/proc/version", RAVEN_VERSION);
    ramfs_write("/proc/uptime", "dynamic uptime available via metrics command");
    ramfs_write("/proc/cmdline", "multiboot /boot/ravenos.kernel");
    wyrd_append("procfs", "mount", "/proc", "ok", "diagnostic pseudo-files exported");
}

void procfs_render(void) {
    vga_set_color(COLOR_LIGHT_CYAN, COLOR_BLACK);
    vga_write("procfs\n");
    vga_set_color(COLOR_LIGHT_GREY, COLOR_BLACK);
    vga_write("  /proc/version = "); vga_write(ramfs_read("/proc/version")); vga_putc('\n');
    vga_write("  /proc/uptime  = ticks "); vga_write_dec(timer_ticks()); vga_putc('\n');
    vga_write("  /proc/cmdline = "); vga_write(ramfs_read("/proc/cmdline")); vga_putc('\n');
}

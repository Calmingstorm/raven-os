#include "kernel.h"

static uint32_t snapshots;

void snapshot_init(void) {
    snapshots = 0;
    wyrd_append("snapshot", "init", "state", "ok", "manual state checkpoint scaffold ready");
}

void snapshot_take(void) {
    snapshots++;
    ramfs_write("/var/snapshot-last", "manual checkpoint recorded; volatile prototype only");
    wyrd_append("snapshot", "take", "manual", "ok", "volatile checkpoint recorded");
    log_write("info", "snapshot", "manual checkpoint recorded");
}

void snapshot_render(void) {
    vga_set_color(COLOR_LIGHT_CYAN, COLOR_BLACK);
    vga_write("snapshots\n");
    vga_set_color(COLOR_LIGHT_GREY, COLOR_BLACK);
    vga_write("  count="); vga_write_dec(snapshots); vga_putc('\n');
    vga_write("  last="); vga_write(ramfs_read("/var/snapshot-last") ? ramfs_read("/var/snapshot-last") : "none"); vga_putc('\n');
}

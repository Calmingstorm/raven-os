#include "kernel.h"

static uint8_t planned;
static uint8_t installed;

void installer_init(void) {
    planned = 0;
    installed = 0;
    wyrd_append("installer", "init", "raven-install", "ok", "install planner ready");
}

void installer_plan(void) {
    planned = 1;
    wyrd_append("installer", "plan", "ravenfs0", "ok", "boot image and base packages selected");
    vga_write("install plan\n");
    vga_write("  target: ravenfs0 volatile block image\n");
    vga_write("  layout: superblock + file slots + wyrd bootstrap files\n");
    vga_write("  packages: base editor net-loop ravenfs\n");
    vga_write("  warning: prototype target is volatile; ATA/virtio persistence still pending\n");
}

int installer_run(void) {
    if (!planned) installer_plan();
    storage_format();
    storage_save_file("/boot/ravenos", "kernel image placeholder copied by installer scaffold");
    storage_save_file("/etc/hostname", env_get("hostname"));
    storage_save_file("/etc/motd", ramfs_read("/etc/motd"));
    storage_save_file("/var/log/wyrd", "wyrd bootstrap log initialized");
    installed = 1;
    wyrd_append("installer", "run", "ravenfs0", "ok", "base system written to block image");
    return 1;
}

void installer_render(void) {
    vga_set_color(COLOR_LIGHT_CYAN, COLOR_BLACK);
    vga_write("installer\n");
    vga_set_color(COLOR_LIGHT_GREY, COLOR_BLACK);
    vga_write("  planned: "); vga_write(planned ? "yes" : "no"); vga_putc('\n');
    vga_write("  installed: "); vga_write(installed ? "yes" : "no"); vga_putc('\n');
    vga_write("  target driver: volatile ravenfs0 now, ATA/virtio next\n");
}

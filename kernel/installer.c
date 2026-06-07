#include "kernel.h"

static uint8_t installer_ready;

void installer_init(void) {
    installer_ready = 1;
    wyrd_append("installer", "init", "image-plan", "ok", "non-destructive installer plan available");
}

void installer_plan(void) {
    vga_write("RavenOS installer plan, dry-run only:\n");
    vga_write("  1. detect boot disk\n");
    vga_write("  2. create raven system partition\n");
    vga_write("  3. write kernel + initrd\n");
    vga_write("  4. install bootloader entry\n");
    vga_write("  5. persist wyrd log seed\n");
    vga_write("No destructive disk writes are implemented yet. Good.\n");
    wyrd_append("installer", "plan", "dry-run", "ok", "install steps rendered");
}

void installer_render(void) {
    vga_set_color(COLOR_LIGHT_CYAN, COLOR_BLACK);
    vga_write("installer\n");
    vga_set_color(COLOR_LIGHT_GREY, COLOR_BLACK);
    vga_write("  ready: "); vga_write_dec(installer_ready); vga_putc('\n');
    vga_write("  mode: dry-run only\n");
    vga_write("  target: future disk-backed RavenFS image\n");
}

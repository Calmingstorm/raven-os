#include "kernel.h"

typedef struct {
    const char *name;
    const char *version;
    const char *state;
    const char *note;
} Package;

static Package packages[] = {
    {"base", RAVEN_VERSION, "installed", "kernel, shell, wyrd timeline"},
    {"ramfs-tools", "0.1", "installed", "cat/write/ls-style inspection"},
    {"net-loopback", "0.1", "installed", "self-test transport scaffold"},
    {"installer", "0.1", "planned", "dry-run image target only"},
    {"gui", "0.0", "missing", "future compositor and panes"},
};

void package_init(void) {
    wyrd_append("pkg", "init", "catalog", "ok", "package registry loaded");
}

void package_render(void) {
    vga_set_color(COLOR_LIGHT_CYAN, COLOR_BLACK);
    vga_write("packages\n");
    vga_set_color(COLOR_LIGHT_GREY, COLOR_BLACK);
    for (uint32_t i = 0; i < sizeof(packages)/sizeof(packages[0]); i++) {
        vga_write("  "); vga_write(packages[i].name); vga_write(" "); vga_write(packages[i].version);
        vga_write(" ["); vga_write(packages[i].state); vga_write("] - "); vga_write(packages[i].note); vga_putc('\n');
    }
}

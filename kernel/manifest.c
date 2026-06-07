#include "kernel.h"

typedef struct {
    const char *component;
    const char *version;
    const char *status;
} ManifestEntry;

static ManifestEntry manifest[] = {
    {"boot.multiboot", "1", "validated"},
    {"kernel.i386", RAVEN_VERSION, "active"},
    {"memory.pmm", "0.2", "active"},
    {"memory.heap", "0.1", "active"},
    {"fs.ramfs", "0.2", "active"},
    {"scheduler.coop", "0.1", "active"},
    {"network.loopback", "0.1", "active"},
    {"security.policy", "0.1", "active"},
};

void manifest_init(void) {
    wyrd_append("manifest", "load", "components", "ok", "boot manifest registered");
}

void manifest_render(void) {
    vga_set_color(COLOR_LIGHT_CYAN, COLOR_BLACK);
    vga_write("system manifest\n");
    vga_set_color(COLOR_LIGHT_GREY, COLOR_BLACK);
    for (uint32_t i = 0; i < sizeof(manifest)/sizeof(manifest[0]); i++) {
        vga_write("  "); vga_write(manifest[i].component); vga_write(" ");
        vga_write(manifest[i].version); vga_write(" "); vga_write(manifest[i].status); vga_putc('\n');
    }
}

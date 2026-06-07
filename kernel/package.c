#include "kernel.h"

typedef struct {
    const char *name;
    const char *version;
    const char *summary;
    uint8_t installed;
} Package;

static Package packages[] = {
    {"base", RAVEN_VERSION, "kernel shell and diagnostics", 1},
    {"editor", "0.1", "line-oriented ramfs editor command", 1},
    {"net-loop", "0.1", "loopback packet/socket tools", 1},
    {"ravenfs", "0.1", "tiny block-image file store", 1},
    {"games", "0.1", "demo userspace programs", 0},
    {"web", "0.1", "future network client shell", 0},
};

void package_init(void) {
    wyrd_append("pkg", "init", "catalog", "ok", "package registry loaded");
}

int package_install(const char *name) {
    for (uint32_t i = 0; i < sizeof(packages)/sizeof(packages[0]); i++) {
        if (!strcmp(packages[i].name, name)) {
            packages[i].installed = 1;
            storage_save_file(packages[i].name[0] ? "/pkg/last" : "/pkg/unknown", packages[i].name);
            wyrd_append("pkg", "install", name, "ok", packages[i].version);
            return 1;
        }
    }
    wyrd_append("pkg", "install", name ? name : "", "fail", "package not found");
    return 0;
}

int package_remove(const char *name) {
    for (uint32_t i = 0; i < sizeof(packages)/sizeof(packages[0]); i++) {
        if (!strcmp(packages[i].name, name)) {
            if (!strcmp(name, "base")) {
                wyrd_append("pkg", "remove", name, "deny", "base package is protected");
                return 0;
            }
            packages[i].installed = 0;
            wyrd_append("pkg", "remove", name, "ok", "package marked absent");
            return 1;
        }
    }
    wyrd_append("pkg", "remove", name ? name : "", "fail", "package not found");
    return 0;
}

int package_is_installed(const char *name) {
    for (uint32_t i = 0; i < sizeof(packages)/sizeof(packages[0]); i++) {
        if (!strcmp(packages[i].name, name)) return packages[i].installed ? 1 : 0;
    }
    return 0;
}

void package_render_installed(void) {
    vga_write("installed packages\n");
    for (uint32_t i = 0; i < sizeof(packages)/sizeof(packages[0]); i++) {
        if (packages[i].installed) {
            vga_write("  "); vga_write(packages[i].name); vga_write(" "); vga_write(packages[i].version);
            vga_write(" - "); vga_write(packages[i].summary); vga_putc('\n');
        }
    }
}

void package_render(void) {
    vga_set_color(COLOR_LIGHT_CYAN, COLOR_BLACK);
    vga_write("package catalog\n");
    vga_set_color(COLOR_LIGHT_GREY, COLOR_BLACK);
    for (uint32_t i = 0; i < sizeof(packages)/sizeof(packages[0]); i++) {
        vga_write("  "); vga_write(packages[i].installed ? "[x] " : "[ ] ");
        vga_write(packages[i].name); vga_write(" "); vga_write(packages[i].version);
        vga_write(" - "); vga_write(packages[i].summary); vga_putc('\n');
    }
}

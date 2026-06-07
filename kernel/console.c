#include "kernel.h"

typedef struct {
    const char *name;
    uint8_t active;
    char last[48];
} ConsoleSession;

static ConsoleSession consoles[] = {
    {"tty0", 1, "primary VGA console"},
    {"tty1", 0, "reserved operator console"},
    {"serial0", 0, "future COM1 diagnostics"},
};

void console_init(void) {
    wyrd_append("console", "init", "sessions", "ok", "multi-console table loaded");
}

void console_switch(const char *name) {
    for (uint32_t i = 0; i < sizeof(consoles)/sizeof(consoles[0]); i++) {
        consoles[i].active = !strcmp(consoles[i].name, name) ? 1 : 0;
    }
    wyrd_append("console", "switch", name, "ok", "active console changed");
}

void console_render(void) {
    vga_set_color(COLOR_LIGHT_CYAN, COLOR_BLACK);
    vga_write("consoles\n");
    vga_set_color(COLOR_LIGHT_GREY, COLOR_BLACK);
    for (uint32_t i = 0; i < sizeof(consoles)/sizeof(consoles[0]); i++) {
        vga_write("  "); vga_write(consoles[i].active ? "* " : "  ");
        vga_write(consoles[i].name); vga_write(" - "); vga_write(consoles[i].last); vga_putc('\n');
    }
}

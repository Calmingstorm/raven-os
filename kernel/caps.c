#include "kernel.h"

static Capability caps[CAPS_CAP];
static uint32_t cap_len;

static void add_cap(const char *name, const char *holder, uint32_t rights, uint8_t granted) {
    if (cap_len >= CAPS_CAP) return;
    caps[cap_len++] = (Capability){ name, holder, rights, granted };
}

void caps_init(void) {
    cap_len = 0;
    add_cap("vga.write", "kernel.console", 0x01, 1);
    add_cap("keyboard.read", "kernel.input", 0x01, 1);
    add_cap("wyrd.append", "kernel", 0x03, 1);
    add_cap("timer.read", "kernel.clock", 0x01, 1);
    add_cap("object.inspect", "shell", 0x01, 1);
    add_cap("panic.trigger", "shell", 0x01, 0);
}

int cap_check(const char *name, const char *holder) {
    for (uint32_t i = 0; i < cap_len; i++) {
        if (!strcmp(caps[i].name, name) && !strcmp(caps[i].holder, holder)) return caps[i].granted;
    }
    return 0;
}

void caps_render(void) {
    vga_set_color(COLOR_LIGHT_CYAN, COLOR_BLACK);
    vga_write("capability table\n");
    vga_set_color(COLOR_LIGHT_GREY, COLOR_BLACK);
    for (uint32_t i = 0; i < cap_len; i++) {
        vga_write("  "); vga_write(caps[i].granted ? "grant" : "deny ");
        vga_write(" | "); vga_write(caps[i].holder);
        vga_write(" | "); vga_write(caps[i].name);
        vga_write(" | rights="); vga_write_hex(caps[i].rights);
        vga_putc('\n');
    }
}

#include "kernel.h"

static KernelObject objects[OBJECT_CAP];
static uint32_t object_len;

static void add_object(const char *path, const char *type, const char *owner, const char *state) {
    if (object_len >= OBJECT_CAP) return;
    objects[object_len++] = (KernelObject){ path, type, owner, state };
}

void objects_init(void) {
    object_len = 0;
    add_object("/dev/vga0", "device.display.text", "kernel.console", "online");
    add_object("/dev/keyboard0", "device.input.ps2", "kernel.input", "irq1");
    add_object("/sys/wyrd", "ring.log", "kernel", "online");
    add_object("/sys/caps", "table.capability", "kernel", "online");
    add_object("/sys/clock", "timer.pit", "kernel.clock", "online");
    add_object("/bin/shell", "task.system", "init", "foreground");
}

void objects_render(void) {
    vga_set_color(COLOR_LIGHT_CYAN, COLOR_BLACK);
    vga_write("object namespace\n");
    vga_set_color(COLOR_LIGHT_GREY, COLOR_BLACK);
    for (uint32_t i = 0; i < object_len; i++) {
        vga_write("  "); vga_write(objects[i].path);
        vga_write(" | "); vga_write(objects[i].type);
        vga_write(" | owner="); vga_write(objects[i].owner);
        vga_write(" | "); vga_write(objects[i].state);
        vga_putc('\n');
    }
}

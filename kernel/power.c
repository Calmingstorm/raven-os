#include "kernel.h"

static uint8_t reboot_requested;
static uint8_t shutdown_requested;

void power_init(void) {
    reboot_requested = 0;
    shutdown_requested = 0;
    wyrd_append("power", "init", "control", "ok", "safe simulation mode");
}

void power_request(const char *action) {
    if (!strcmp(action, "reboot")) {
        reboot_requested = 1;
        wyrd_append("power", "request", "reboot", "simulated", "hardware reset disabled in prototype");
        log_write("warn", "power", "reboot request recorded, not executed");
    } else if (!strcmp(action, "shutdown")) {
        shutdown_requested = 1;
        wyrd_append("power", "request", "shutdown", "simulated", "ACPI poweroff disabled in prototype");
        log_write("warn", "power", "shutdown request recorded, not executed");
    }
}

void power_render(void) {
    vga_set_color(COLOR_LIGHT_CYAN, COLOR_BLACK);
    vga_write("power state\n");
    vga_set_color(COLOR_LIGHT_GREY, COLOR_BLACK);
    vga_write("  reboot_requested="); vga_write(reboot_requested ? "yes" : "no"); vga_putc('\n');
    vga_write("  shutdown_requested="); vga_write(shutdown_requested ? "yes" : "no"); vga_putc('\n');
    vga_write("  mode=safe simulation, no real reset/poweroff yet\n");
}

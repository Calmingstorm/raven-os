#include "kernel.h"

typedef struct {
    const char *name;
    const char *state;
    uint32_t restarts;
    const char *note;
} Service;

#define SERVICE_CAP 12
static Service services[SERVICE_CAP];
static uint32_t service_len;

static void add_service(const char *name, const char *state, const char *note) {
    if (service_len >= SERVICE_CAP) return;
    services[service_len].name = name;
    services[service_len].state = state;
    services[service_len].restarts = 0;
    services[service_len].note = note;
    service_len++;
}

void service_init(void) {
    service_len = 0;
    add_service("shell", "running", "operator command palette");
    add_service("wyrd", "running", "native event timeline");
    add_service("ramfs", "running", "volatile filesystem");
    add_service("net.loopback", "running", "self-test packet path");
    add_service("installer", "idle", "dry-run install planner");
    wyrd_append("svc", "init", "manager", "ok", "service table online");
}

int service_restart(const char *name) {
    for (uint32_t i = 0; i < service_len; i++) {
        if (!strcmp(services[i].name, name)) {
            services[i].state = "running";
            services[i].restarts++;
            wyrd_append("svc", "restart", name, "ok", "service restart simulated");
            log_write("info", "svc", "service restart simulated");
            return 1;
        }
    }
    wyrd_append("svc", "restart", name, "fail", "service not found");
    return 0;
}

void service_render(void) {
    vga_set_color(COLOR_LIGHT_CYAN, COLOR_BLACK);
    vga_write("services\n");
    vga_set_color(COLOR_LIGHT_GREY, COLOR_BLACK);
    for (uint32_t i = 0; i < service_len; i++) {
        vga_write("  "); vga_write(services[i].name); vga_write("  "); vga_write(services[i].state);
        vga_write("  restarts="); vga_write_dec(services[i].restarts);
        vga_write("  "); vga_write(services[i].note); vga_putc('\n');
    }
}

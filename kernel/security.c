#include "kernel.h"

typedef struct {
    const char *subject;
    const char *action;
    const char *target;
    uint8_t allow;
} PolicyRule;

static PolicyRule rules[] = {
    {"shell", "read", "/sys", 1},
    {"shell", "write", "/notes", 1},
    {"shell", "trigger", "panic", 0},
    {"user.demo", "syscall", "ticks", 1},
    {"user.demo", "write", "/kernel", 0},
};

void security_init(void) {
    wyrd_append("security", "init", "policy", "ok", "deny-by-default scaffold loaded");
}

int security_check(const char *subject, const char *action, const char *target) {
    for (uint32_t i = 0; i < sizeof(rules)/sizeof(rules[0]); i++) {
        if (!strcmp(rules[i].subject, subject) && !strcmp(rules[i].action, action) && !strcmp(rules[i].target, target)) {
            wyrd_append("security", rules[i].allow ? "allow" : "deny", target, rules[i].allow ? "ok" : "blocked", subject);
            return rules[i].allow ? 1 : 0;
        }
    }
    wyrd_append("security", "deny", target, "blocked", "no matching allow rule");
    return 0;
}

void security_render(void) {
    vga_set_color(COLOR_LIGHT_CYAN, COLOR_BLACK);
    vga_write("security policy\n");
    vga_set_color(COLOR_LIGHT_GREY, COLOR_BLACK);
    for (uint32_t i = 0; i < sizeof(rules)/sizeof(rules[0]); i++) {
        vga_write("  "); vga_write(rules[i].subject); vga_write(" "); vga_write(rules[i].action);
        vga_write(" "); vga_write(rules[i].target); vga_write(" => "); vga_write(rules[i].allow ? "allow" : "deny"); vga_putc('\n');
    }
}

#include "kernel.h"

typedef struct {
    const char *name;
    uint32_t entry_syscall;
    const char *state;
    uint32_t runs;
} UserProgram;

#define USER_CAP 8
static UserProgram programs[USER_CAP];
static uint32_t user_len;

static void add_user(const char *name, uint32_t syscall_num, const char *state) {
    if (user_len >= USER_CAP) return;
    programs[user_len++] = (UserProgram){ name, syscall_num, state, 0 };
}

void user_init(void) {
    user_len = 0;
    add_user("/bin/about", 3, "ready");
    add_user("/bin/tick", 2, "ready");
    add_user("/bin/wyrd-note", 1, "ready");
    task_spawn("user.about", "runnable");
    task_spawn("user.tick", "runnable");
    wyrd_append("userspace", "init", "program-table", "ok", "ABI-backed user program scaffold loaded");
}

void user_run_demo(void) {
    for (uint32_t i = 0; i < user_len; i++) {
        uint32_t r = syscall_dispatch(programs[i].entry_syscall, 0, 0, 0);
        programs[i].runs++;
        vga_write("ran "); vga_write(programs[i].name);
        vga_write(" -> "); vga_write_hex(r); vga_putc('\n');
    }
    wyrd_append("userspace", "run", "demo-programs", "ok", "programs called kernel ABI");
}

void user_render(void) {
    vga_set_color(COLOR_LIGHT_CYAN, COLOR_BLACK);
    vga_write("userspace program table\n");
    vga_set_color(COLOR_LIGHT_GREY, COLOR_BLACK);
    vga_write("  ring3: pending; current programs execute as supervised ABI probes\n");
    for (uint32_t i = 0; i < user_len; i++) {
        vga_write("  "); vga_write(programs[i].name);
        vga_write(" syscall="); vga_write_dec(programs[i].entry_syscall);
        vga_write(" state="); vga_write(programs[i].state);
        vga_write(" runs="); vga_write_dec(programs[i].runs); vga_putc('\n');
    }
}

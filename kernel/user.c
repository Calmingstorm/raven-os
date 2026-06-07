#include "kernel.h"

typedef struct {
    char name[32];
    const char *entry;
    const char *state;
    uint32_t runs;
} UserProgram;

#define USER_CAP 8
static UserProgram programs[USER_CAP];
static uint32_t program_count;

static void add_program(const char *name, const char *entry, const char *state) {
    if (program_count >= USER_CAP) return;
    strcopy(programs[program_count].name, name, sizeof(programs[program_count].name));
    programs[program_count].entry = entry;
    programs[program_count].state = state;
    programs[program_count].runs = 0;
    program_count++;
}

void user_init(void) {
    program_count = 0;
    memset(programs, 0, sizeof(programs));
    add_program("hello", "sys_write + sys_ticks", "available");
    add_program("clock", "sys_ticks", "available");
    add_program("writer", "ramfs write", "available");
    add_program("netdemo", "loopback send", "available");
    wyrd_append("user", "init", "program-table", "ok", "supervised demo programs registered");
}

static int run_program(uint32_t i) {
    if (i >= program_count) return 0;
    programs[i].runs++;
    uint32_t tid = task_spawn(programs[i].name, "running");
    wyrd_append("user", "exec", programs[i].name, "ok", programs[i].entry);
    if (!strcmp(programs[i].name, "hello")) {
        syscall_dispatch(1, 0, 0, 0);
        vga_write("user.hello: hello from supervised userspace scaffold\n");
    } else if (!strcmp(programs[i].name, "clock")) {
        uint32_t t = syscall_dispatch(2, 0, 0, 0);
        vga_write("user.clock: ticks="); vga_write_dec(t); vga_putc('\n');
    } else if (!strcmp(programs[i].name, "writer")) {
        if (security_check("user.demo", "write", "/kernel")) {
            vga_write("user.writer: unexpected kernel write allow\n");
        } else {
            ramfs_write("/home/user-note", "written by user.writer through supervised API");
            vga_write("user.writer: wrote /home/user-note after kernel-write denial proof\n");
        }
    } else if (!strcmp(programs[i].name, "netdemo")) {
        int sid = net_open_socket("user.netdemo", "127.0.0.1");
        net_send((uint32_t)sid, "hello network stack");
        vga_write("user.netdemo: sent loopback payload\n");
    }
    task_set_state(tid, "exited");
    return 1;
}

int user_run_named(const char *name) {
    for (uint32_t i = 0; i < program_count; i++) {
        if (!strcmp(programs[i].name, name)) return run_program(i);
    }
    wyrd_append("user", "exec", name ? name : "", "fail", "program not found");
    return 0;
}

void user_run_demo(void) {
    for (uint32_t i = 0; i < program_count; i++) run_program(i);
}

void user_render(void) {
    vga_set_color(COLOR_LIGHT_CYAN, COLOR_BLACK);
    vga_write("userspace programs\n");
    vga_set_color(COLOR_LIGHT_GREY, COLOR_BLACK);
    for (uint32_t i = 0; i < program_count; i++) {
        vga_write("  "); vga_write(programs[i].name); vga_write(" entry="); vga_write(programs[i].entry);
        vga_write(" state="); vga_write(programs[i].state); vga_write(" runs="); vga_write_dec(programs[i].runs); vga_putc('\n');
    }
}

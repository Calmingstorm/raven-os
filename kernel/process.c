#include "kernel.h"

typedef struct {
    uint8_t used;
    uint32_t pid;
    char name[32];
    uint32_t page_dir;
    uint32_t entry;
    uint32_t user_stack;
    const char *state;
} Process;

#define PROC_CAP 8
static Process procs[PROC_CAP];
static uint32_t next_pid;
static uint32_t proc_count;
static uint32_t current_pid;

void process_init(void) {
    memset(procs, 0, sizeof(procs));
    next_pid = 1;
    proc_count = 0;
    current_pid = 0;
    process_spawn("init", 0x00400000);
    wyrd_append("proc", "init", "process-table", "ok", "address-space metadata online");
}

uint32_t process_spawn(const char *name, uint32_t entry) {
    for (uint32_t i = 0; i < PROC_CAP; i++) {
        if (!procs[i].used) {
            procs[i].used = 1;
            procs[i].pid = next_pid++;
            strcopy(procs[i].name, name ? name : "proc", sizeof(procs[i].name));
            procs[i].page_dir = 0x00800000 + procs[i].pid * PAGE_SIZE;
            procs[i].entry = entry;
            procs[i].user_stack = 0xBFFFF000 - procs[i].pid * PAGE_SIZE;
            procs[i].state = "ready";
            proc_count++;
            if (!current_pid) current_pid = procs[i].pid;
            wyrd_append("proc", "spawn", procs[i].name, "ok", "process metadata created");
            return procs[i].pid;
        }
    }
    wyrd_append("proc", "spawn", name ? name : "proc", "fail", "process table full");
    return 0;
}

int process_kill(uint32_t pid) {
    for (uint32_t i = 0; i < PROC_CAP; i++) {
        if (procs[i].used && procs[i].pid == pid) {
            procs[i].used = 0;
            if (proc_count) proc_count--;
            wyrd_append("proc", "kill", procs[i].name, "ok", "process metadata removed");
            return 1;
        }
    }
    return 0;
}

void process_render(void) {
    vga_set_color(COLOR_LIGHT_CYAN, COLOR_BLACK);
    vga_write("processes\n");
    vga_set_color(COLOR_LIGHT_GREY, COLOR_BLACK);
    vga_write("  count: "); vga_write_dec(proc_count); vga_write(" current_pid: "); vga_write_dec(current_pid); vga_putc('\n');
    for (uint32_t i = 0; i < PROC_CAP; i++) {
        if (procs[i].used) {
            vga_write("  pid="); vga_write_dec(procs[i].pid); vga_write(" "); vga_write(procs[i].name);
            vga_write(" state="); vga_write(procs[i].state);
            vga_write(" entry="); vga_write_hex(procs[i].entry);
            vga_write(" pd="); vga_write_hex(procs[i].page_dir);
            vga_write(" ustack="); vga_write_hex(procs[i].user_stack); vga_putc('\n');
        }
    }
    vga_write("  next: bind these metadata address spaces to CR3 and iret into ring-3\n");
}

#include "kernel.h"

typedef struct {
    uint32_t id;
    const char *name;
    const char *state;
    uint32_t ticks;
} Task;

#define TASK_CAP 16
static Task tasks[TASK_CAP];
static uint32_t task_len;
static uint32_t current;
static uint32_t next_id;

void task_init(void) {
    task_len = 0;
    current = 0;
    next_id = 1;
    task_spawn("kernel.idle", "ready");
    task_spawn("init.shell", "foreground");
    wyrd_append("sched", "init", "task-table", "ok", "cooperative scheduler scaffold online");
}

uint32_t task_spawn(const char *name, const char *state) {
    if (task_len >= TASK_CAP) return 0;
    tasks[task_len] = (Task){ next_id++, name, state, 0 };
    wyrd_append("sched", "spawn", name, "ok", "task registered");
    return tasks[task_len++].id;
}

void task_tick(void) {
    if (!task_len) return;
    tasks[current].ticks++;
    current = (current + 1) % task_len;
}

void task_render(void) {
    vga_set_color(COLOR_LIGHT_CYAN, COLOR_BLACK);
    vga_write("task table\n");
    vga_set_color(COLOR_LIGHT_GREY, COLOR_BLACK);
    for (uint32_t i = 0; i < task_len; i++) {
        vga_write(i == current ? "* " : "  ");
        vga_write("id="); vga_write_dec(tasks[i].id);
        vga_write(" name="); vga_write(tasks[i].name);
        vga_write(" state="); vga_write(tasks[i].state);
        vga_write(" ticks="); vga_write_dec(tasks[i].ticks); vga_putc('\n');
    }
}

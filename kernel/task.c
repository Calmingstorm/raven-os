#include "kernel.h"

typedef struct {
    uint32_t id;
    char name[32];
    const char *state;
    uint32_t ticks;
    uint8_t alive;
} Task;

#define TASK_CAP 16
static Task tasks[TASK_CAP];
static uint32_t task_count;
static uint32_t next_id;
static uint32_t current_slot;
static uint32_t switches;
static uint32_t preemptive_enabled;

void task_init(void) {
    memset(tasks, 0, sizeof(tasks));
    task_count = 0;
    next_id = 1;
    current_slot = 0;
    switches = 0;
    preemptive_enabled = 1;
    task_spawn("kernel.idle", "running");
    task_spawn("service.shell", "ready");
    task_spawn("service.logger", "ready");
    wyrd_append("sched", "init", "round-robin", "ok", "preemption accounting online");
}

uint32_t task_spawn(const char *name, const char *state) {
    for (uint32_t i = 0; i < TASK_CAP; i++) {
        if (!tasks[i].alive) {
            tasks[i].id = next_id++;
            strcopy(tasks[i].name, name ? name : "task", sizeof(tasks[i].name));
            tasks[i].state = state ? state : "ready";
            tasks[i].ticks = 0;
            tasks[i].alive = 1;
            if (task_count < TASK_CAP) task_count++;
            wyrd_append("sched", "spawn", tasks[i].name, "ok", tasks[i].state);
            return tasks[i].id;
        }
    }
    wyrd_append("sched", "spawn", name ? name : "task", "fail", "task table full");
    return 0;
}

int task_kill(uint32_t id) {
    for (uint32_t i = 0; i < TASK_CAP; i++) {
        if (tasks[i].alive && tasks[i].id == id) {
            tasks[i].alive = 0;
            if (task_count) task_count--;
            wyrd_append("sched", "kill", tasks[i].name, "ok", "task removed");
            return 1;
        }
    }
    wyrd_append("sched", "kill", "task", "miss", "id not found");
    return 0;
}

int task_set_state(uint32_t id, const char *state) {
    for (uint32_t i = 0; i < TASK_CAP; i++) {
        if (tasks[i].alive && tasks[i].id == id) {
            tasks[i].state = state ? state : "ready";
            wyrd_append("sched", "state", tasks[i].name, "ok", tasks[i].state);
            return 1;
        }
    }
    return 0;
}

static uint32_t next_alive(uint32_t start) {
    for (uint32_t step = 0; step < TASK_CAP; step++) {
        uint32_t idx = (start + step) % TASK_CAP;
        if (tasks[idx].alive) return idx;
    }
    return start;
}

void task_tick(void) {
    for (uint32_t i = 0; i < TASK_CAP; i++) {
        if (tasks[i].alive) tasks[i].ticks++;
    }
    if (preemptive_enabled && task_count > 1) {
        uint32_t old = current_slot;
        current_slot = next_alive(current_slot + 1);
        if (current_slot != old) switches++;
    }
}

uint32_t task_switches(void) { return switches; }
void task_set_preemptive(uint32_t enabled) { preemptive_enabled = enabled ? 1 : 0; }

void task_render(void) {
    vga_set_color(COLOR_LIGHT_CYAN, COLOR_BLACK);
    vga_write("tasks\n");
    vga_set_color(COLOR_LIGHT_GREY, COLOR_BLACK);
    vga_write("  alive: "); vga_write_dec(task_count);
    vga_write(" switches: "); vga_write_dec(switches);
    vga_write(" preemptive-accounting: "); vga_write(preemptive_enabled ? "on" : "off"); vga_putc('\n');
    for (uint32_t i = 0; i < TASK_CAP; i++) {
        if (tasks[i].alive) {
            vga_write(i == current_slot ? " *#" : "  #"); vga_write_dec(tasks[i].id); vga_write(" "); vga_write(tasks[i].name);
            vga_write(" state="); vga_write(tasks[i].state);
            vga_write(" ticks="); vga_write_dec(tasks[i].ticks); vga_putc('\n');
        }
    }
    vga_write("  note: timer IRQ now rotates runnable task accounting; stack/context switch pending\n");
}

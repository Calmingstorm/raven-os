#include "kernel.h"

typedef struct {
    uint32_t tick;
    char level[8];
    char source[24];
    char message[96];
    uint8_t used;
} LogLine;

#define LOG_CAP 64
static LogLine log_lines[LOG_CAP];
static uint32_t log_next;
static uint32_t log_count;

void log_init(void) {
    log_next = 0;
    log_count = 0;
    log_write("info", "kernel", "structured log online");
    wyrd_append("log", "init", "ring", "ok", "64 diagnostic lines reserved");
}

void log_write(const char *level, const char *source, const char *message) {
    LogLine *l = &log_lines[log_next % LOG_CAP];
    l->tick = timer_ticks();
    strcopy(l->level, level, sizeof(l->level));
    strcopy(l->source, source, sizeof(l->source));
    strcopy(l->message, message, sizeof(l->message));
    l->used = 1;
    log_next++;
    if (log_count < LOG_CAP) log_count++;
}

void log_render(uint32_t limit) {
    if (!limit || limit > log_count) limit = log_count;
    vga_set_color(COLOR_LIGHT_CYAN, COLOR_BLACK);
    vga_write("system log\n");
    vga_set_color(COLOR_LIGHT_GREY, COLOR_BLACK);
    uint32_t start = log_next > limit ? log_next - limit : 0;
    for (uint32_t i = start; i < log_next; i++) {
        LogLine *l = &log_lines[i % LOG_CAP];
        if (!l->used) continue;
        vga_write("  t+"); vga_write_dec(l->tick);
        vga_write(" "); vga_write(l->level);
        vga_write(" "); vga_write(l->source);
        vga_write(": "); vga_write(l->message); vga_putc('\n');
    }
}

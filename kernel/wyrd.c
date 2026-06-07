#include "kernel.h"

static WyrdEvent wyrd[WYRD_CAP];
static uint32_t wyrd_next;
static uint32_t logical_tick;

void wyrd_init(void) {
    memset(wyrd, 0, sizeof(wyrd));
    wyrd_next = 0;
    logical_tick = 0;
}

void wyrd_append(const char *actor, const char *verb, const char *object, const char *result, const char *note) {
    WyrdEvent *e = &wyrd[wyrd_next++ % WYRD_CAP];
    e->tick = ++logical_tick;
    e->actor = actor;
    e->verb = verb;
    e->object = object;
    e->result = result;
    e->note = note;
}

uint32_t wyrd_count(void) {
    return wyrd_next < WYRD_CAP ? wyrd_next : WYRD_CAP;
}

void wyrd_render(uint32_t limit) {
    uint32_t count = wyrd_count();
    if (limit && limit < count) count = limit;
    uint32_t start = wyrd_next > count ? wyrd_next - count : 0;
    vga_set_color(COLOR_LIGHT_CYAN, COLOR_BLACK);
    vga_write("wyrd timeline\n");
    vga_set_color(COLOR_LIGHT_GREY, COLOR_BLACK);
    for (uint32_t i = 0; i < count; i++) {
        WyrdEvent *e = &wyrd[(start + i) % WYRD_CAP];
        vga_write("  #"); vga_write_dec(e->tick);
        vga_write(" | "); vga_write(e->actor);
        vga_write(" | "); vga_write(e->verb);
        vga_write(" | "); vga_write(e->object);
        vga_write(" | "); vga_write(e->result);
        vga_write(" | "); vga_write(e->note);
        vga_putc('\n');
    }
}

#include "kernel.h"

static WyrdEvent wyrd[WYRD_CAP];
static uint32_t wyrd_next;
static uint32_t retained;
static uint32_t logical_tick;

void wyrd_init(void) {
    memset(wyrd, 0, sizeof(wyrd));
    wyrd_next = 0;
    retained = 0;
    logical_tick = 0;
}

/* Check one byte beyond the retained prefix to distinguish exactly-full from
 * truncated fields. All storage belongs to the ring, never the caller or heap. */
static uint8_t copy_field(char *dst, size_t cap, const char *src, uint8_t flag) {
    size_t i = 0;
    if (!src) return 0;
    while (i + 1 < cap && src[i]) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
    return src[i] ? flag : 0;
}

void wyrd_append(const char *actor, const char *verb, const char *object, const char *result, const char *note) {
    WyrdEvent *e = &wyrd[wyrd_next];
    memset(e, 0, sizeof(*e));
    e->tick = ++logical_tick;
    e->truncated |= copy_field(e->actor, sizeof(e->actor), actor, WYRD_TRUNC_ACTOR);
    e->truncated |= copy_field(e->verb, sizeof(e->verb), verb, WYRD_TRUNC_VERB);
    e->truncated |= copy_field(e->object, sizeof(e->object), object, WYRD_TRUNC_OBJECT);
    e->truncated |= copy_field(e->result, sizeof(e->result), result, WYRD_TRUNC_RESULT);
    e->truncated |= copy_field(e->note, sizeof(e->note), note, WYRD_TRUNC_NOTE);
    wyrd_next = (wyrd_next + 1) % WYRD_CAP;
    if (retained < WYRD_CAP) retained++;
}

uint32_t wyrd_count(void) { return retained; }

static WyrdEvent *event_at(uint32_t oldest_index) {
    return &wyrd[(wyrd_next + WYRD_CAP - retained + oldest_index) % WYRD_CAP];
}

int wyrd_get(uint32_t oldest_index, WyrdEvent *out) {
    if (!out || oldest_index >= retained) return 0;
    memcpy(out, event_at(oldest_index), sizeof(*out));
    return 1;
}

static void render_truncation(uint8_t flags) {
    if (!flags) return;
    static const char *const fields[] = {"actor", "verb", "object", "result", "note"};
    vga_write(" [truncated:");
    int first = 1;
    for (uint32_t bit = 0; bit < 5; bit++) {
        if (!(flags & (1u << bit))) continue;
        if (!first) vga_putc(',');
        vga_write(fields[bit]);
        first = 0;
    }
    vga_putc(']');
}

void wyrd_render(uint32_t limit) {
    uint32_t count = wyrd_count();
    if (limit && limit < count) count = limit;
    uint32_t start = retained - count;
    vga_set_color(COLOR_LIGHT_CYAN, COLOR_BLACK);
    vga_write("wyrd timeline\n");
    vga_set_color(COLOR_LIGHT_GREY, COLOR_BLACK);
    for (uint32_t i = 0; i < count; i++) {
        WyrdEvent *e = event_at(start + i);
        vga_write("  #"); vga_write_dec(e->tick);
        vga_write(" | "); vga_write(e->actor);
        vga_write(" | "); vga_write(e->verb);
        vga_write(" | "); vga_write(e->object);
        vga_write(" | "); vga_write(e->result);
        vga_write(" | "); vga_write(e->note);
        render_truncation(e->truncated);
        vga_putc('\n');
    }
}

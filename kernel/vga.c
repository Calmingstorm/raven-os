#include "kernel.h"

#ifdef VGA_TEST
/* The host harness executes this driver with VRAM and I/O replaced. */
extern volatile uint16_t vga_test_vram[VGA_WIDTH * VGA_HEIGHT];
#define VGA vga_test_vram
#else
static volatile uint16_t *const VGA = (uint16_t *)0xB8000;
#endif
#define HISTORY_LINES 256u
#define INPUT_MAX 159u

/* Independent of both VRAM and the heap: browsing never destroys live output. */
static uint16_t live[VGA_HEIGHT][VGA_WIDTH];
static uint16_t history[HISTORY_LINES][VGA_WIDTH];
static size_t history_head;
static size_t history_count;
static size_t view_back;
static size_t row;
static size_t col;
static uint8_t color;
static uint8_t cursor_start;
static uint8_t cursor_end;
static int input_active;
static size_t input_row;
static size_t input_col;
static size_t input_len;
static size_t input_cursor;

static uint8_t crtc_read(uint8_t index) {
    outb(0x3D4, index);
    return inb(0x3D5);
}

static void crtc_write(uint8_t index, uint8_t value) {
    outb(0x3D4, index);
    outb(0x3D5, value);
}

static void cursor_update(void) {
    size_t y = row;
    size_t x = col;
    if (input_active) {
        y = input_row + (input_col + input_cursor) / VGA_WIDTH;
        x = (input_col + input_cursor) % VGA_WIDTH;
    }
    uint16_t pos = (uint16_t)(y * VGA_WIDTH + x);
    crtc_write(0x0E, (uint8_t)(pos >> 8));
    crtc_write(0x0F, (uint8_t)pos);
    /* Bit 5 disables the cursor. Keep the firmware's scan-line shape. */
    crtc_write(0x0A, (uint8_t)((cursor_start & ~0x20u) | (view_back ? 0x20u : 0)));
    crtc_write(0x0B, cursor_end);
}

static void render(void) {
    for (size_t y = 0; y < VGA_HEIGHT; y++) {
        const uint16_t *src;
        if (y < view_back) {
            size_t index = (history_head + history_count - view_back + y) % HISTORY_LINES;
            src = history[index];
        } else {
            src = live[y - view_back];
        }
        for (size_t x = 0; x < VGA_WIDTH; x++) VGA[y * VGA_WIDTH + x] = src[x];
    }
}

static void live_cell(size_t y, size_t x, uint16_t cell) {
    live[y][x] = cell;
    if (y + view_back < VGA_HEIGHT) VGA[(y + view_back) * VGA_WIDTH + x] = cell;
}

static uint16_t entry(char c, uint8_t ccolor) {
    return (uint16_t)(uint8_t)c | (uint16_t)ccolor << 8;
}

static uint8_t make_color(uint8_t fg, uint8_t bg) {
    return fg | (bg << 4);
}

static void scroll(void) {
    size_t index = (history_head + history_count) % HISTORY_LINES;
    for (size_t x = 0; x < VGA_WIDTH; x++) history[index][x] = live[0][x];
    if (history_count < HISTORY_LINES) history_count++;
    else history_head = (history_head + 1) % HISTORY_LINES;
    /* Keep a browsed viewport anchored until its oldest line is evicted. */
    if (view_back && view_back < history_count) view_back++;
    for (size_t y = 1; y < VGA_HEIGHT; y++) {
        for (size_t x = 0; x < VGA_WIDTH; x++) live[y - 1][x] = live[y][x];
    }
    for (size_t x = 0; x < VGA_WIDTH; x++) live[VGA_HEIGHT - 1][x] = entry(' ', color);
    row = VGA_HEIGHT - 1;
    if (input_active && input_row) input_row--;
    render();
}

void vga_init(void) {
    cursor_start = crtc_read(0x0A);
    cursor_end = crtc_read(0x0B);
    row = 0;
    col = 0;
    color = make_color(COLOR_LIGHT_GREY, COLOR_BLACK);
    vga_clear();
}

void vga_clear(void) {
    for (size_t y = 0; y < VGA_HEIGHT; y++) {
        for (size_t x = 0; x < VGA_WIDTH; x++) live[y][x] = entry(' ', color);
    }
    history_head = 0;
    history_count = 0;
    view_back = 0;
    input_active = 0;
    input_row = input_col = input_len = input_cursor = 0;
    row = 0;
    col = 0;
    render();
    cursor_update();
}

void vga_scrollback(int lines) {
    if (lines > 0) {
        size_t amount = (size_t)lines;
        size_t available = history_count - view_back;
        view_back += amount < available ? amount : available;
    } else if (lines < 0) {
        /* Avoid negating INT_MIN. */
        size_t amount = (size_t)(-(lines + 1)) + 1;
        view_back -= amount < view_back ? amount : view_back;
    }
    render();
    cursor_update();
}

void vga_live(void) {
    if (view_back) {
        view_back = 0;
        render();
    }
    cursor_update();
}

void vga_input_begin(void) {
    input_row = row;
    input_col = col;
    input_len = input_cursor = 0;
    input_active = 1;
    cursor_update();
}

void vga_input_redraw(const char *text, uint32_t len, uint32_t cursor) {
    if (!input_active) return;
    vga_live();
    if (!text) len = 0;
    if (len > INPUT_MAX) len = INPUT_MAX;
    if (cursor > len) cursor = len;
    /* Erase the entire previous extent, not merely the new string's tail. */
    for (size_t i = 0; i < input_len; i++) {
        size_t offset = input_col + i;
        live_cell(input_row + offset / VGA_WIDTH, offset % VGA_WIDTH, entry(' ', color));
    }
    /* Reserve space for the END position as well as every rendered cell. */
    while (input_row + (input_col + len) / VGA_WIDTH >= VGA_HEIGHT) scroll();
    for (size_t i = 0; i < len; i++) {
        size_t offset = input_col + i;
        live_cell(input_row + offset / VGA_WIDTH, offset % VGA_WIDTH, entry(text[i], color));
    }
    input_len = len;
    input_cursor = cursor;
    row = input_row + (input_col + len) / VGA_WIDTH;
    col = (input_col + len) % VGA_WIDTH;
    cursor_update();
}

void vga_input_end(void) {
    input_active = 0;
    cursor_update();
}

void vga_set_color(uint8_t fg, uint8_t bg) {
    color = make_color(fg, bg);
}

void vga_putc(char c) {
    serial_putc(c);
    input_active = 0;
    if (c == '\n') {
        col = 0;
        if (++row >= VGA_HEIGHT) scroll();
        cursor_update();
        return;
    }
    if (c == '\r') { col = 0; cursor_update(); return; }
    live_cell(row, col, entry(c, color));
    if (++col >= VGA_WIDTH) {
        col = 0;
        if (++row >= VGA_HEIGHT) scroll();
    }
    cursor_update();
}

void vga_write(const char *s) {
    while (s && *s) vga_putc(*s++);
}

void vga_write_dec(uint32_t n) {
    char buf[11];
    size_t i = 0;
    if (n == 0) { vga_putc('0'); return; }
    while (n && i < sizeof(buf)) { buf[i++] = (char)('0' + n % 10); n /= 10; }
    while (i) vga_putc(buf[--i]);
}

void vga_write_hex(uint32_t n) {
    const char *hex = "0123456789ABCDEF";
    vga_write("0x");
    for (int i = 7; i >= 0; i--) vga_putc(hex[(n >> (i * 4)) & 0xF]);
}

void vga_status(const char *left, const char *right) {
    uint8_t old = color;
    uint8_t st = make_color(COLOR_BLACK, COLOR_LIGHT_GREY);
    for (size_t x = 0; x < VGA_WIDTH; x++) live_cell(VGA_HEIGHT - 1, x, entry(' ', st));
    for (size_t x = 0; left && left[x] && x < VGA_WIDTH; x++) live_cell(VGA_HEIGHT - 1, x, entry(left[x], st));
    size_t rlen = right ? strlen(right) : 0;
    size_t start = rlen < VGA_WIDTH ? VGA_WIDTH - rlen : 0;
    for (size_t x = 0; right && right[x] && start + x < VGA_WIDTH; x++) live_cell(VGA_HEIGHT - 1, start + x, entry(right[x], st));
    color = old;
}

void vga_draw_prompt(const char *line) {
    vga_set_color(COLOR_LIGHT_GREEN, COLOR_BLACK);
    vga_write("raven> ");
    vga_set_color(COLOR_WHITE, COLOR_BLACK);
    vga_write(line);
}

void vga_backspace(void) {
    input_active = 0;
    if (col > 0) {
        col--;
    } else if (row > 0) {
        row--;
        col = VGA_WIDTH - 1;
    } else {
        cursor_update();
        return;
    }
    live_cell(row, col, entry(' ', color));
    cursor_update();
}

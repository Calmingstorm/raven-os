#include "kernel.h"

static volatile uint16_t *const VGA = (uint16_t *)0xB8000;
static size_t row;
static size_t col;
static uint8_t color;

static uint16_t entry(char c, uint8_t ccolor) {
    return (uint16_t)c | (uint16_t)ccolor << 8;
}

static uint8_t make_color(uint8_t fg, uint8_t bg) {
    return fg | (bg << 4);
}

static void scroll(void) {
    for (size_t y = 1; y < VGA_HEIGHT; y++) {
        for (size_t x = 0; x < VGA_WIDTH; x++) VGA[(y - 1) * VGA_WIDTH + x] = VGA[y * VGA_WIDTH + x];
    }
    for (size_t x = 0; x < VGA_WIDTH; x++) VGA[(VGA_HEIGHT - 1) * VGA_WIDTH + x] = entry(' ', color);
    row = VGA_HEIGHT - 1;
}

void vga_init(void) {
    row = 0;
    col = 0;
    color = make_color(COLOR_LIGHT_GREY, COLOR_BLACK);
    vga_clear();
}

void vga_clear(void) {
    for (size_t y = 0; y < VGA_HEIGHT; y++) {
        for (size_t x = 0; x < VGA_WIDTH; x++) VGA[y * VGA_WIDTH + x] = entry(' ', color);
    }
    row = 0;
    col = 0;
}

void vga_set_color(uint8_t fg, uint8_t bg) {
    color = make_color(fg, bg);
}

void vga_putc(char c) {
    if (c == '\n') {
        col = 0;
        if (++row >= VGA_HEIGHT) scroll();
        return;
    }
    if (c == '\r') { col = 0; return; }
    VGA[row * VGA_WIDTH + col] = entry(c, color);
    if (++col >= VGA_WIDTH) {
        col = 0;
        if (++row >= VGA_HEIGHT) scroll();
    }
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
    volatile uint16_t *line = VGA + (VGA_HEIGHT - 1) * VGA_WIDTH;
    uint8_t st = make_color(COLOR_BLACK, COLOR_LIGHT_GREY);
    for (size_t x = 0; x < VGA_WIDTH; x++) line[x] = entry(' ', st);
    for (size_t x = 0; left && left[x] && x < VGA_WIDTH; x++) line[x] = entry(left[x], st);
    size_t rlen = strlen(right);
    size_t start = rlen < VGA_WIDTH ? VGA_WIDTH - rlen : 0;
    for (size_t x = 0; right && right[x] && start + x < VGA_WIDTH; x++) line[start + x] = entry(right[x], st);
    color = old;
}

void vga_draw_prompt(const char *line) {
    vga_set_color(COLOR_LIGHT_GREEN, COLOR_BLACK);
    vga_write("raven> ");
    vga_set_color(COLOR_WHITE, COLOR_BLACK);
    vga_write(line);
}

void vga_backspace(void) {
    if (col > 0) {
        col--;
    } else if (row > 0) {
        row--;
        col = VGA_WIDTH - 1;
    } else {
        return;
    }
    VGA[row * VGA_WIDTH + col] = entry(' ', color);
}

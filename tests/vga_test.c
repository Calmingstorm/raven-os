/* Execute the production driver, substituting only its hardware boundary. */
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include "../kernel/kernel.h"

volatile uint16_t vga_test_vram[VGA_WIDTH * VGA_HEIGHT];
static uint8_t crtc[256];
static uint8_t crtc_index;
static size_t serial_count;
static char serial_bytes[65536];

void outb(uint16_t port, uint8_t value) {
    assert(port == 0x3D4 || port == 0x3D5);
    if (port == 0x3D4) crtc_index = value;
    else crtc[crtc_index] = value;
}

uint8_t inb(uint16_t port) {
    assert(port == 0x3D5);
    return crtc[crtc_index];
}

void serial_putc(char c) {
    assert(serial_count < sizeof(serial_bytes));
    serial_bytes[serial_count] = c;
    serial_count++;
}

#define VGA_TEST
#include "../kernel/vga.c"

static size_t cursor_position(void) {
    return ((size_t)crtc[0x0E] << 8) | crtc[0x0F];
}

static uint16_t screen(size_t y, size_t x) {
    assert(y < VGA_HEIGHT && x < VGA_WIDTH);
    return vga_test_vram[y * VGA_WIDTH + x];
}

static void reset(void) {
    /* Include a reserved high bit and firmware's disabled bit. */
    crtc[0x0A] = 0x66;
    crtc[0x0B] = 0xA7;
    serial_count = 0;
    vga_init();
    assert(crtc[0x0A] == 0x46);
    assert(crtc[0x0B] == 0xA7);
    assert(cursor_position() == 0);
}

static void test_wrap_scroll_backspace(void) {
    reset();
    vga_set_color(COLOR_LIGHT_RED, COLOR_BLUE);
    for (size_t i = 0; i < VGA_WIDTH; i++) vga_putc('W');
    assert(cursor_position() == VGA_WIDTH);
    vga_backspace();
    assert(cursor_position() == VGA_WIDTH - 1);
    assert(screen(0, 79) == entry(' ', 0x1C));
    vga_putc('Z');
    for (size_t i = 1; i < VGA_HEIGHT; i++) vga_putc('\n');
    assert(cursor_position() == 24 * VGA_WIDTH);
    assert(history_count == 1);
    assert(history[history_head][79] == entry('Z', 0x1C));
    vga_scrollback(1);
    assert(screen(0, 79) == entry('Z', 0x1C));
    assert(crtc[0x0A] == 0x66);
    assert(crtc[0x0B] == 0xA7);
    vga_live();
    assert(crtc[0x0A] == 0x46);
    assert(screen(0, 79) == entry(' ', COLOR_LIGHT_GREY));
    vga_putc('\r');
    assert(cursor_position() == 24 * VGA_WIDTH);
    assert(serial_count == VGA_WIDTH + VGA_HEIGHT + 1);
    for (size_t i = 0; i < VGA_WIDTH; i++) assert(serial_bytes[i] == 'W');
    assert(serial_bytes[VGA_WIDTH] == 'Z');
    for (size_t i = VGA_WIDTH + 1; i < serial_count - 1; i++) assert(serial_bytes[i] == '\n');
    assert(serial_bytes[serial_count - 1] == '\r');
    puts("vga: wrap, row scroll, backspace and CRTC shape/visibility passed");
}

static uint16_t numbered_cell(size_t line, size_t x) {
    uint8_t attribute = (uint8_t)(((line / 16) % 8) << 4 | (line % 16));
    return entry((char)('A' + (line + x) % 26), attribute);
}

static void test_colored_history(void) {
    reset();
    /* 330 lines with distinct attributes/contents, beyond ring rollover. */
    for (size_t line = 0; line < 330; line++) {
        uint8_t attribute = (uint8_t)(numbered_cell(line, 0) >> 8);
        vga_set_color(attribute & 15, attribute >> 4);
        for (size_t x = 0; x < VGA_WIDTH - 1; x++)
            vga_putc((char)numbered_cell(line, x));
        vga_putc('\n');
    }
    assert(history_count == 256);
    /* 306 rows scrolled off, therefore only historical rows 50..305 remain. */
    vga_scrollback(INT_MAX);
    assert(view_back == 256);
    for (size_t back = 256; back > 0; back--) {
        assert(view_back == back);
        size_t first_line = 306 - back;
        for (size_t y = 0; y < VGA_HEIGHT; y++) {
            for (size_t x = 0; x < VGA_WIDTH - 1; x++)
                assert(screen(y, x) == numbered_cell(first_line + y, x));
        }
        vga_scrollback(-1);
    }
    assert(view_back == 0);
    assert(!(crtc[0x0A] & 0x20));
    vga_scrollback(INT_MAX);
    assert(screen(0, 0) == numbered_cell(50, 0));
    vga_scrollback(INT_MIN);
    assert(view_back == 0);
    assert(screen(0, 0) == numbered_cell(306, 0));
    /* Background output preserves the selected rows until eviction. */
    vga_scrollback(10);
    uint16_t saved = screen(0, 0);
    vga_putc('N');
    vga_putc('\n');
    assert(view_back == 11);
    assert(screen(0, 0) == saved);
    vga_scrollback(INT_MAX);
    vga_putc('\n');
    assert(view_back == 256);
    assert(screen(0, 0) == numbered_cell(52, 0));
    vga_clear();
    vga_scrollback(INT_MAX);
    assert(view_back == 0 && history_count == 0);
    assert(cursor_position() == 0 && !(crtc[0x0A] & 0x20));
    for (size_t y = 0; y < VGA_HEIGHT; y++)
        for (size_t x = 0; x < VGA_WIDTH; x++)
            assert((screen(y, x) & 255) == ' ');
    puts("vga: all 256 colored history lines, eviction, browse/live and clear passed");
}

static void check_input(const char *text, size_t len, size_t edit_cursor) {
    assert(row == input_row + (input_col + len) / VGA_WIDTH);
    assert(col == (input_col + len) % VGA_WIDTH);
    assert(cursor_position() == input_row * VGA_WIDTH + input_col + edit_cursor);
    for (size_t i = 0; i < len; i++) {
        size_t offset = input_col + i;
        assert(screen(input_row + offset / VGA_WIDTH, offset % VGA_WIDTH) == entry(text[i], 15));
    }
}

static void test_editor(void) {
    reset();
    for (size_t y = 0; y < 24; y++) vga_putc('\n');
    vga_set_color(COLOR_LIGHT_GREEN, COLOR_BLACK);
    vga_write("raven> ");
    vga_set_color(COLOR_WHITE, COLOR_BLACK);
    vga_input_begin();
    char text[INPUT_MAX + 1];
    for (size_t i = 0; i < INPUT_MAX; i++) text[i] = (char)('a' + i % 26);
    text[INPUT_MAX] = 0;
    size_t echoed = serial_count;
    vga_input_redraw(text, 72, 20);
    check_input(text, 72, 20);
    assert(input_row == 24 && history_count == 0);
    /* Inserting across the row boundary reserves room for the end cursor. */
    memmove(text + 21, text + 20, 52);
    text[20] = 'X';
    vga_input_redraw(text, 73, 21);
    check_input(text, 73, 21);
    assert(input_row == 23 && history_count == 1);
    assert(screen(23, 6) == entry(' ', 10));
    assert(screen(23, 0) == entry('r', 10));
    /* Delete back across the boundary, leaving no stale trailing cell. */
    memmove(text + 20, text + 21, 52);
    vga_input_redraw(text, 72, 20);
    check_input(text, 72, 20);
    assert(screen(23, 79) == entry(' ', 15));
    vga_input_redraw(text, INPUT_MAX, 79);
    check_input(text, INPUT_MAX, 79);
    assert(input_row == 22 && history_count == 2);
    assert(screen(22, 0) == entry('r', 10));
    vga_scrollback(1);
    assert(crtc[0x0A] & 0x20);
    vga_live();
    assert(!(crtc[0x0A] & 0x20));
    check_input(text, INPUT_MAX, 79);
    vga_input_redraw("hi", 2, 1);
    check_input("hi", 2, 1);
    for (size_t offset = 9; offset < 7 + INPUT_MAX; offset++)
        assert(screen(22 + offset / VGA_WIDTH, offset % VGA_WIDTH) == entry(' ', 15));
    assert(history_count == 2); /* Shrinking must not scroll again. */
    assert(serial_count == echoed); /* Redraw is VGA-only. */
    vga_input_end();
    assert(cursor_position() == 22 * VGA_WIDTH + 9);
    vga_putc('\n');
    assert(cursor_position() == 23 * VGA_WIDTH);
    assert(serial_count == echoed + 1);
    /* Empty input, cursor clamping and deterministic clear while editing. */
    vga_input_begin();
    vga_input_redraw("hi", 2, UINT32_MAX);
    check_input("hi", 2, 2);
    vga_input_redraw(NULL, 100, 99);
    check_input("", 0, 0);
    vga_clear();
    vga_input_redraw(text, INPUT_MAX, 100);
    assert(cursor_position() == 0 && history_count == 0);
    puts("vga: bottom-row wrapped insert/delete/shrink, editing cursor and serial isolation passed");
}

static void test_editor_all_rows(void) {
    /* Independently derive the anchor and screen cells for each legal length.
     * Exercise both growth and shrink at every possible prompt row. */
    char text[INPUT_MAX];
    for (size_t i = 0; i < sizeof(text); i++) text[i] = (char)('A' + i % 26);
    for (size_t prompt_row = 0; prompt_row < VGA_HEIGHT; prompt_row++) {
        reset();
        for (size_t y = 0; y < prompt_row; y++) vga_putc('\n');
        vga_set_color(COLOR_LIGHT_GREEN, COLOR_BLACK);
        vga_write("raven> ");
        vga_set_color(COLOR_WHITE, COLOR_BLACK);
        vga_input_begin();
        size_t anchor = prompt_row;
        size_t rendered = 0;
        size_t scrolls = 0;
        size_t mirrored = serial_count;
        for (size_t step = 0; step <= 2 * INPUT_MAX; step++) {
            size_t len = step <= INPUT_MAX ? step : 2 * INPUT_MAX - step;
            size_t extent = len > rendered ? len : rendered;
            while (anchor + (7 + len) / VGA_WIDTH >= VGA_HEIGHT) {
                anchor--;
                scrolls++;
            }
            size_t cursor = len / 2;
            vga_input_redraw(text, (uint32_t)len, (uint32_t)cursor);
            assert(cursor_position() == anchor * VGA_WIDTH + 7 + cursor);
            assert(row == anchor + (7 + len) / VGA_WIDTH);
            assert(col == (7 + len) % VGA_WIDTH);
            assert(history_count == scrolls);
            assert(serial_count == mirrored);
            assert(screen(anchor, 0) == entry('r', COLOR_LIGHT_GREEN));
            for (size_t i = 0; i < extent; i++) {
                size_t offset = 7 + i;
                assert(screen(anchor + offset / VGA_WIDTH, offset % VGA_WIDTH) ==
                       entry(i < len ? text[i] : ' ', COLOR_WHITE));
            }
            rendered = len;
        }
        /* Defensive bound enforcement must not read past 159 source bytes. */
        vga_input_redraw(text, UINT32_MAX, UINT32_MAX);
        assert(cursor_position() == row * VGA_WIDTH + col);
        assert(input_len == INPUT_MAX && input_cursor == INPUT_MAX);
        assert(serial_count == mirrored);
    }
    puts("vga: exhaustive 0..159 growth/shrink at all 25 prompt rows passed");
}

int main(void) {
    test_wrap_scroll_backspace();
    test_colored_history();
    test_editor();
    test_editor_all_rows();
    puts("vga: all production-driver tests passed");
    return 0;
}

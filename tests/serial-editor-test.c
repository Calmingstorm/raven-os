/* Test the production serial editor against an independent 80x25 VT model.
 * Link with function-section GC: unrelated command-dispatch services are not
 * needed for this focused hardware-boundary regression test. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../kernel/kernel.h"

static char cells[VGA_HEIGHT][VGA_WIDTH];
static unsigned y, x, pending_wrap, escape, parameter;

static void newline(void) {
    if (++y == VGA_HEIGHT) {
        memmove(cells[0], cells[1], (VGA_HEIGHT - 1) * VGA_WIDTH);
        memset(cells[VGA_HEIGHT - 1], ' ', VGA_WIDTH);
        y--;
    }
}

void serial_putc(char c) {
    if (escape == 1) { assert(c == '['); escape = 2; parameter = 0; return; }
    if (escape == 2) {
        if (c >= '0' && c <= '9') { parameter = parameter * 10 + (unsigned)(c - '0'); return; }
        unsigned n = parameter ? parameter : 1;
        pending_wrap = 0;
        if (c == 'A') y = n <= y ? y - n : 0;
        else if (c == 'C') x = x + n < VGA_WIDTH ? x + n : VGA_WIDTH - 1;
        else if (c == 'J') {
            assert(!parameter);
            memset(cells[y] + x, ' ', VGA_WIDTH - x);
            for (unsigned i = y + 1; i < VGA_HEIGHT; i++) memset(cells[i], ' ', VGA_WIDTH);
        } else assert(0);
        escape = 0; return;
    }
    if (c == '\033') { escape = 1; return; }
    if (c == '\r') { x = pending_wrap = 0; return; }
    /* Production serial_putc translates LF to CRLF. */
    if (c == '\n') { x = pending_wrap = 0; newline(); return; }
    assert(c >= 32 && c <= 126);
    if (pending_wrap) { pending_wrap = 0; x = 0; newline(); }
    cells[y][x] = c;
    if (++x == VGA_WIDTH) { x--; pending_wrap = 1; }
}

void serial_write(const char *s) { while (*s) serial_putc(*s++); }
void vga_set_color(uint8_t fg, uint8_t bg) { (void)fg; (void)bg; }
void vga_write(const char *s) { serial_write(s); }
void vga_input_begin(void) {}

#include "../kernel/shell.c"

static void reset(unsigned origin) {
    memset(cells, ' ', sizeof(cells));
    y = origin; x = pending_wrap = escape = parameter = 0;
    line_len = line_cursor = 0; line[0] = 0;
    prompt();
}

static void check(unsigned origin) {
    unsigned end_row = origin + (7 + line_len) / VGA_WIDTH;
    if (end_row >= VGA_HEIGHT) origin -= end_row - (VGA_HEIGHT - 1);
    assert(!escape && !pending_wrap);
    assert(y == origin + (7 + line_cursor) / VGA_WIDTH);
    assert(x == (7 + line_cursor) % VGA_WIDTH);
    assert(!memcmp(cells[origin], "raven> ", 7));
    for (unsigned i = 0; i < line_len; i++)
        assert(cells[origin + (7 + i) / VGA_WIDTH][(7 + i) % VGA_WIDTH] == line[i]);
    for (unsigned offset = 7 + line_len; offset < (VGA_HEIGHT - origin) * VGA_WIDTH; offset++)
        assert(cells[origin + offset / VGA_WIDTH][offset % VGA_WIDTH] == ' ');
}

int main(void) {
    for (unsigned origin = 0; origin < VGA_HEIGHT; origin++) {
        reset(origin);
        unsigned anchor = origin;
        for (unsigned i = 0; i < 159; i++) {
            line[line_len++] = (char)('a' + i % 26);
            line[line_len] = 0; line_cursor = line_len;
            serial_append(line[i]);
            if (anchor + (7 + line_len) / VGA_WIDTH >= VGA_HEIGHT) anchor--;
            check(anchor);
        }
        /* Home, middle, End, backspace across every wrap boundary. */
        for (unsigned cursor = 0; cursor <= line_len; cursor++) {
            line_cursor = cursor; serial_redraw(); check(anchor);
        }
        line_cursor = 0; serial_redraw(); check(anchor);
        /* Enter from the middle must address the end before printing LF. */
        serial_redraw_at(line_len);
        assert(y == anchor + (7 + line_len) / VGA_WIDTH);
        assert(x == (7 + line_len) % VGA_WIDTH);
        serial_putc('\n');
        unsigned next_row = anchor + (7 + line_len) / VGA_WIDTH + 1;
        if (next_row == VGA_HEIGHT) next_row--;
        assert(y == next_row && x == 0);
        /* Return to the command under test with a newly established prompt. */
        reset(origin); anchor = origin;
        for (unsigned len = 1; len <= 159; len++) {
            line[len - 1] = (char)('a' + len % 26); line[len] = 0;
            line_len = len; line_cursor = len / 2;
            serial_redraw();
            if (anchor + (7 + len) / VGA_WIDTH >= VGA_HEIGHT) anchor--;
            check(anchor);
        }
        for (unsigned len = 158; len > 0; len--) {
            line_len = line_cursor = len; line[len] = 0;
            serial_redraw(); check(anchor);
        }
        /* Ctrl+U resets the whole line; a new command must leave no old rows. */
        line_len = line_cursor = 0; line[0] = 0;
        serial_redraw(); check(anchor);
        strcopy(line, "help", sizeof(line)); line_len = line_cursor = 4;
        serial_redraw(); check(anchor);
        /* Completion/new command/clear all establish a fresh prompt origin. */
        serial_putc('\n'); serial_write("help  heap\n"); prompt();
        anchor = y;
        strcopy(line, "he", sizeof(line)); line_len = line_cursor = 2;
        serial_redraw(); check(anchor);
        reset(origin);
        serial_redraw(); check(origin);
    }
    puts("serial editor: 159-char wrap, all cursor positions, shrink, Ctrl+U, Enter and prompt reset passed at all 25 rows");
    return 0;
}

#include "kernel.h"

#define COM1 0x3F8
static uint8_t serial_ready;
static uint8_t serial_echo_input;

static int serial_transmit_empty(void) {
    return inb(COM1 + 5) & 0x20;
}

void serial_init(void) {
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x80);
    outb(COM1 + 0, 0x03); /* 38400 baud divisor */
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03); /* 8N1 */
    outb(COM1 + 2, 0xC7); /* FIFO */
    outb(COM1 + 4, 0x0B); /* IRQs enabled, RTS/DSR */
    serial_ready = 1;
    serial_echo_input = 0;
    wyrd_append("driver.serial", "init", "com1", "ok", "debug console online");
}

void serial_putc(char c) {
    if (!serial_ready) return;
    if (c == '\n') serial_putc('\r');
    uint32_t spin = 100000;
    while (!serial_transmit_empty() && spin--) {}
    outb(COM1, (uint8_t)c);
}

void serial_write(const char *s) {
    while (s && *s) serial_putc(*s++);
}

int serial_can_read(void) {
    if (!serial_ready) return 0;
    return inb(COM1 + 5) & 1;
}

int serial_read_char(char *out) {
    if (!serial_can_read()) return 0;
    char c = (char)inb(COM1);
    if (c == '\r') c = '\n';
    *out = c;
    if (serial_echo_input && c >= 32) serial_putc(c);
    return 1;
}

void serial_render(void) {
    vga_set_color(COLOR_LIGHT_CYAN, COLOR_BLACK);
    vga_write("serial console\n");
    vga_set_color(COLOR_LIGHT_GREY, COLOR_BLACK);
    vga_write("  com1: "); vga_write(serial_ready ? "online" : "offline"); vga_putc('\n');
    vga_write("  baud: 38400  mode: 8N1  mirror: vga output -> serial\n");
    vga_write("  scripted validation can drive the shell over stdio now\n");
}

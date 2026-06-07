#include "kernel.h"

enum { SYS_WYRD = 1, SYS_TICKS = 2, SYS_READ_VERSION = 3 };
static uint32_t syscall_count;

void syscall_init(void) {
    syscall_count = 0;
    wyrd_append("abi", "init", "syscall-table", "ok", "kernel ABI dispatch scaffold online");
}

uint32_t syscall_dispatch(uint32_t num, uint32_t a, uint32_t b, uint32_t c) {
    (void)a; (void)b; (void)c;
    syscall_count++;
    if (num == SYS_WYRD) {
        wyrd_append("userspace", "syscall", "wyrd", "ok", "event accepted through ABI");
        return 0;
    }
    if (num == SYS_TICKS) return timer_ticks();
    if (num == SYS_READ_VERSION) return (uint32_t)RAVEN_VERSION;
    wyrd_append("userspace", "syscall", "unknown", "fail", "bad syscall number");
    return 0xFFFFFFFFu;
}

void syscall_render(void) {
    vga_set_color(COLOR_LIGHT_CYAN, COLOR_BLACK);
    vga_write("syscall ABI\n");
    vga_set_color(COLOR_LIGHT_GREY, COLOR_BLACK);
    vga_write("  dispatches: "); vga_write_dec(syscall_count); vga_putc('\n');
    vga_write("  1 SYS_WYRD         append userspace event\n");
    vga_write("  2 SYS_TICKS        return PIT ticks\n");
    vga_write("  3 SYS_READ_VERSION return version string pointer\n");
    vga_write("  note: int gate/ring3 transition pending; dispatch is kernel-callable now\n");
}

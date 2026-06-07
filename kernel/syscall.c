#include "kernel.h"

enum { SYS_WYRD = 1, SYS_TICKS = 2, SYS_READ_VERSION = 3, SYS_WRITE_NOTE = 4, SYS_SEND_LOOP = 5 };
static uint32_t syscall_count;
static uint32_t int80_count;

void syscall_init(void) {
    syscall_count = 0;
    int80_count = 0;
    wyrd_append("abi", "init", "syscall-table", "ok", "int 0x80 gate and dispatcher online");
}

uint32_t syscall_dispatch(uint32_t num, uint32_t a, uint32_t b, uint32_t c) {
    (void)b; (void)c;
    syscall_count++;
    if (num == SYS_WYRD) {
        wyrd_append("userspace", "syscall", "wyrd", "ok", "event accepted through ABI");
        return 0;
    }
    if (num == SYS_TICKS) return timer_ticks();
    if (num == SYS_READ_VERSION) return (uint32_t)RAVEN_VERSION;
    if (num == SYS_WRITE_NOTE) {
        ramfs_write("/home/syscall-note", a ? (const char *)a : "note from syscall");
        return 0;
    }
    if (num == SYS_SEND_LOOP) {
        int sid = net_open_socket("syscall.loop", "127.0.0.1");
        return net_send((uint32_t)sid, a ? (const char *)a : "syscall payload") ? 0 : 1;
    }
    wyrd_append("userspace", "syscall", "unknown", "fail", "bad syscall number");
    return 0xFFFFFFFFu;
}

uint32_t syscall_from_registers(Registers *regs) {
    int80_count++;
    wyrd_append("abi", "trap", "int80", "ok", "register syscall received");
    return syscall_dispatch(regs->eax, regs->ebx, regs->ecx, regs->edx);
}

void syscall_render(void) {
    vga_set_color(COLOR_LIGHT_CYAN, COLOR_BLACK);
    vga_write("syscall ABI\n");
    vga_set_color(COLOR_LIGHT_GREY, COLOR_BLACK);
    vga_write("  dispatches: "); vga_write_dec(syscall_count); vga_putc('\n');
    vga_write("  int80 traps: "); vga_write_dec(int80_count); vga_putc('\n');
    vga_write("  1 SYS_WYRD         append userspace event\n");
    vga_write("  2 SYS_TICKS        return PIT ticks\n");
    vga_write("  3 SYS_READ_VERSION return version string pointer\n");
    vga_write("  4 SYS_WRITE_NOTE   write /home/syscall-note\n");
    vga_write("  5 SYS_SEND_LOOP    send loopback socket payload\n");
    vga_write("  gate: int 0x80 present at DPL3; ring-3 transition remains the next wall\n");
}

#include "kernel.h"

void kernel_main(void) {
    vga_init();
    vga_set_color(COLOR_LIGHT_CYAN, COLOR_BLACK);
    vga_write("RavenOS " RAVEN_VERSION " booting...\n");
    vga_set_color(COLOR_LIGHT_GREY, COLOR_BLACK);

    wyrd_init();
    wyrd_append("boot", "enter", "kernel_main", "ok", "stack online");
    wyrd_append("driver.vga", "mode", "text80x25", "ok", "memory mapped at 0xB8000");
    wyrd_append("kernel", "init", "wyrd-ring", "ok", "256 events reserved");

    caps_init();
    wyrd_append("kernel", "init", "capability-table", "ok", "least-authority scaffold loaded");
    objects_init();
    wyrd_append("kernel", "init", "object-namespace", "ok", "inspectable kernel objects loaded");

    idt_init();
    wyrd_append("cpu", "init", "idt+pic", "ok", "interrupt gates installed");
    timer_init(100);
    keyboard_init();

    __asm__ volatile ("sti");
    wyrd_append("cpu", "interrupts", "sti", "ok", "timer and keyboard live");

    shell_init();
    for (;;) {
        shell_tick();
        __asm__ volatile ("hlt");
    }
}

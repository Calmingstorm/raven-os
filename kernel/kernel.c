#include "kernel.h"

void kernel_main(uint32_t multiboot_magic, uint32_t multiboot_info_addr) {
    vga_init();
    vga_set_color(COLOR_LIGHT_CYAN, COLOR_BLACK);
    vga_write("RavenOS " RAVEN_VERSION " booting...\n");
    vga_set_color(COLOR_LIGHT_GREY, COLOR_BLACK);

    wyrd_init();
    wyrd_append("boot", "enter", "kernel_main", "ok", "stack online");
    wyrd_append("driver.vga", "mode", "text80x25", "ok", "memory mapped at 0xB8000");
    wyrd_append("kernel", "init", "wyrd-ring", "ok", "256 events reserved");
    gdt_init();

    bootinfo_init(multiboot_magic, multiboot_info_addr);

    caps_init();
    wyrd_append("kernel", "init", "capability-table", "ok", "least-authority scaffold loaded");
    objects_init();
    wyrd_append("kernel", "init", "object-namespace", "ok", "inspectable kernel objects loaded");
    pmm_init();
    paging_init();
    heap_init();
    ramfs_init();
    procfs_init();
    ata_init();
    storage_init();
    net_init();
    env_init();
    clock_init();
    log_init();
    package_init();
    power_init();
    security_init();
    service_init();
    console_init();
    manifest_init();
    snapshot_init();
    installer_init();
    syscall_init();
    process_init();
    task_init();
    user_init();
    exec_init();

    idt_init();
    wyrd_append("cpu", "init", "idt+pic", "ok", "interrupt gates installed");
    timer_init(100);
    keyboard_init();

    __asm__ volatile ("sti");
    wyrd_append("cpu", "interrupts", "sti", "ok", "timer and keyboard live");

    log_write("info", "boot", "kernel initialization complete");
    shell_init();
    for (;;) {
        shell_tick();
        __asm__ volatile ("hlt");
    }
}

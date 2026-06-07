#include "kernel.h"

#define PAGE_PRESENT 0x1
#define PAGE_RW 0x2
#define PAGE_SIZE_FLAG 0x80

static uint32_t page_directory[1024] __attribute__((aligned(4096)));
static uint32_t first_page_table[1024] __attribute__((aligned(4096)));
static uint8_t paging_enabled;

static void load_page_directory(uint32_t *pd) {
    __asm__ volatile("mov %0, %%cr3" :: "r"(pd));
}

static void enable_paging(void) {
    uint32_t cr0;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80000000u;
    __asm__ volatile("mov %0, %%cr0" :: "r"(cr0));
}

void paging_init(void) {
    for (uint32_t i = 0; i < 1024; i++) {
        page_directory[i] = 0x00000002;
        first_page_table[i] = (i * PAGE_SIZE) | PAGE_PRESENT | PAGE_RW;
    }
    page_directory[0] = ((uint32_t)first_page_table) | PAGE_PRESENT | PAGE_RW;
    load_page_directory(page_directory);
    enable_paging();
    paging_enabled = 1;
    wyrd_append("memory", "init", "paging", "ok", "identity map first 4MiB enabled");
}

void paging_render(void) {
    vga_set_color(COLOR_LIGHT_CYAN, COLOR_BLACK);
    vga_write("paging\n");
    vga_set_color(COLOR_LIGHT_GREY, COLOR_BLACK);
    vga_write("  enabled: "); vga_write_dec(paging_enabled); vga_putc('\n');
    vga_write("  directory: "); vga_write_hex((uint32_t)page_directory); vga_putc('\n');
    vga_write("  identity:  0x00000000-0x003FFFFF present rw\n");
    vga_write("  note:      first-stage kernel map, not process isolation yet\n");
}

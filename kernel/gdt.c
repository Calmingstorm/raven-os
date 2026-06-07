#include "kernel.h"

typedef struct __attribute__((packed)) {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t base_mid;
    uint8_t access;
    uint8_t granularity;
    uint8_t base_high;
} GdtEntry;

typedef struct __attribute__((packed)) {
    uint16_t limit;
    uint32_t base;
} GdtPointer;

typedef struct __attribute__((packed)) {
    uint32_t prev_tss;
    uint32_t esp0;
    uint32_t ss0;
    uint32_t esp1;
    uint32_t ss1;
    uint32_t esp2;
    uint32_t ss2;
    uint32_t cr3;
    uint32_t eip;
    uint32_t eflags;
    uint32_t eax, ecx, edx, ebx;
    uint32_t esp, ebp, esi, edi;
    uint32_t es, cs, ss, ds, fs, gs;
    uint32_t ldt;
    uint16_t trap;
    uint16_t iomap_base;
} TssEntry;

static GdtEntry gdt[6];
static GdtPointer gdtp;
static TssEntry tss;
static uint8_t gdt_ready;

static void set_gdt_entry(uint32_t n, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran) {
    gdt[n].base_low = base & 0xFFFF;
    gdt[n].base_mid = (base >> 16) & 0xFF;
    gdt[n].base_high = (base >> 24) & 0xFF;
    gdt[n].limit_low = limit & 0xFFFF;
    gdt[n].granularity = ((limit >> 16) & 0x0F) | (gran & 0xF0);
    gdt[n].access = access;
}

static void flush_gdt(void) {
    __asm__ volatile(
        "lgdt (%0)\n"
        "mov $0x10, %%ax\n"
        "mov %%ax, %%ds\n"
        "mov %%ax, %%es\n"
        "mov %%ax, %%fs\n"
        "mov %%ax, %%gs\n"
        "mov %%ax, %%ss\n"
        "ljmp $0x08, $1f\n"
        "1:\n"
        : : "r"(&gdtp) : "ax", "memory");
}

static void load_tss(void) {
    __asm__ volatile("ltr %%ax" : : "a"((uint16_t)0x28));
}

void gdt_init(void) {
    memset(&gdt, 0, sizeof(gdt));
    memset(&tss, 0, sizeof(tss));
    gdtp.limit = sizeof(gdt) - 1;
    gdtp.base = (uint32_t)&gdt;

    set_gdt_entry(0, 0, 0, 0, 0);
    set_gdt_entry(1, 0, 0xFFFFF, 0x9A, 0xCF); /* kernel code */
    set_gdt_entry(2, 0, 0xFFFFF, 0x92, 0xCF); /* kernel data */
    set_gdt_entry(3, 0, 0xFFFFF, 0xFA, 0xCF); /* user code */
    set_gdt_entry(4, 0, 0xFFFFF, 0xF2, 0xCF); /* user data */

    tss.ss0 = 0x10;
    tss.esp0 = 0;
    tss.iomap_base = sizeof(TssEntry);
    set_gdt_entry(5, (uint32_t)&tss, sizeof(TssEntry) - 1, 0x89, 0x40);

    flush_gdt();
    load_tss();
    gdt_ready = 1;
    wyrd_append("cpu", "init", "gdt+tss", "ok", "kernel/user segments and tss loaded");
}

void gdt_set_kernel_stack(uint32_t stack_top) {
    tss.esp0 = stack_top;
    wyrd_append("cpu", "tss", "esp0", "ok", "kernel stack updated");
}

uint32_t gdt_user_code_selector(void) { return 0x1B; }
uint32_t gdt_user_data_selector(void) { return 0x23; }

void gdt_render(void) {
    vga_set_color(COLOR_LIGHT_CYAN, COLOR_BLACK);
    vga_write("rings / descriptor tables\n");
    vga_set_color(COLOR_LIGHT_GREY, COLOR_BLACK);
    vga_write("  gdt loaded: "); vga_write(gdt_ready ? "yes" : "no"); vga_putc('\n');
    vga_write("  kernel code/data selectors: 0x08 / 0x10\n");
    vga_write("  user code/data selectors:   0x1B / 0x23\n");
    vga_write("  tss selector: 0x28 esp0="); vga_write_hex(tss.esp0); vga_putc('\n');
    vga_write("  syscall gate: int 0x80 DPL3 dispatches through kernel ABI\n");
    vga_write("  next hard step: real iret transition into isolated ring-3 address spaces\n");
}

/* Minimal disposable Multiboot guest: links the production memory files and
 * linker script. No storage driver or writable disk exists in this test. */
#include "../kernel/kernel.h"

extern uint32_t kernel_start, kernel_end;
static MultibootInfo test_info;
static MultibootMmapEntry test_map[4];
static uint32_t assertions;

void outb(uint16_t port, uint8_t value) { __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port)); }
uint8_t inb(uint16_t port) { uint8_t value; __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port)); return value; }
void *memset(void *dst, int value, size_t n) { unsigned char *p = dst; while (n--) *p++ = (unsigned char)value; return dst; }
void *memcpy(void *dst, const void *src, size_t n) { unsigned char *d = dst; const unsigned char *s = src; while (n--) *d++ = *s++; return dst; }
void wyrd_append(const char *a, const char *b, const char *c, const char *d, const char *e) {
    (void)a; (void)b; (void)c; (void)d; (void)e;
}
void vga_set_color(uint8_t a, uint8_t b) { (void)a; (void)b; }
void vga_putc(char c) { while (!(inb(0x3fd) & 0x20)) {} outb(0x3f8, (uint8_t)c); }
void vga_write(const char *s) { while (*s) vga_putc(*s++); }
void vga_write_hex(uint32_t n) {
    vga_write("0x");
    for (int shift = 28; shift >= 0; shift -= 4) vga_putc("0123456789ABCDEF"[(n >> shift) & 15]);
}
void vga_write_dec(uint32_t n) {
    char buf[11]; unsigned i = 0;
    do { buf[i++] = (char)('0' + n % 10); n /= 10; } while (n);
    while (i) vga_putc(buf[--i]);
}

static void check(int ok) {
    assertions++;
    if (!ok) {
        vga_write("MEMORY FAIL assertion "); vga_write_dec(assertions); vga_putc('\n');
        outb(0xf4, 0x11);
        for (;;) __asm__ volatile("cli; hlt");
    }
}

static void test_init(void) {
    test_info.flags = 0x40;
    test_info.mmap_addr = (uintptr_t)test_map;
    test_info.mmap_length = sizeof(test_map);
    bootinfo_init(MULTIBOOT_BOOTLOADER_MAGIC, (uintptr_t)&test_info);
    pmm_init();
}

void kernel_main(uint32_t magic, uint32_t info_addr) {
    outb(0x3f9, 0); outb(0x3fb, 0x80); outb(0x3f8, 3);
    outb(0x3f9, 0); outb(0x3fb, 3); outb(0x3fa, 0xc7); outb(0x3fc, 0xb);
    check((uintptr_t)&kernel_start == 0x100000);
    check((uintptr_t)&kernel_end < 0x180000);
    check(magic == MULTIBOOT_BOOTLOADER_MAGIC);
    MultibootInfo *real = (void *)(uintptr_t)info_addr;
    check(real->flags & 0x40);
    /* Exercise real GRUB map with the previously failing mmap-only flags. */
    real->flags &= ~1u;
    bootinfo_init(magic, info_addr); pmm_init();
    bootinfo_render(); pmm_render();
    check(bootinfo_mem_kib() > 0);
    check(pmm_free_frames() > 0);
    uint32_t expected_top = real->mmap_length ? pmm_total_frames() : 0;
    check(expected_top > 256 && expected_top <= PMM_MAX_FRAMES);
    uint32_t frame = pmm_alloc_frame();
    check(frame >= (uintptr_t)&kernel_end && frame < 0x8000000);
    uint32_t before = pmm_free_frames();
    pmm_free_frame(0x100000); pmm_free_frame(frame + 1);
    check(pmm_free_frames() == before);
    pmm_free_frame(frame); check(pmm_free_frames() == before + 1);
    vga_write("MEMORY PASS real GRUB mmap-only allocation and kernel reservation\n");

    test_map[0] = (MultibootMmapEntry){20, 0x280001, 1, 2};
    test_map[1] = (MultibootMmapEntry){20, 0x180000, 0x280000, 1};
    test_map[2] = (MultibootMmapEntry){20, 0x200000, 0x80000, 3};
    test_map[3] = (MultibootMmapEntry){20, 0x100000000ULL, 0x100000000ULL, 1};
    test_init();
    check(pmm_free_frames() == 511);
    uint32_t count = 0;
    while ((frame = pmm_alloc_frame())) {
        check(frame >= 0x180000 && frame < 0x400000);
        check(!(frame >= 0x200000 && frame <= 0x280000)); count++;
    }
    check(count == 511 && pmm_free_frames() == 0);
    pmm_free_frame(0x200000); pmm_free_frame(0x280000);
    check(pmm_free_frames() == 0);
    vga_write("MEMORY PASS synthetic holes, overlapping reservation, high address and exhaustion\n");

    test_map[3].addr = UINT64_MAX - 1; test_map[3].len = 4;
    test_info.flags |= 1; test_info.mem_upper = 127 * 1024;
    bootinfo_init(magic, (uintptr_t)&test_info); pmm_init();
    check(pmm_free_frames() == 0 && pmm_total_frames() == 0);
    check(pmm_alloc_frame() == 0);
    vga_write("MEMORY PASS malformed mmap fails closed despite basic total\n");
    vga_write("MEMORY PASS all assertions="); vga_write_dec(assertions); vga_putc('\n');
    outb(0xf4, 0x10);
    for (;;) __asm__ volatile("cli; hlt");
}

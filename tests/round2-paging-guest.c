/* Disposable guest using production paging, PMM, bootinfo, GDT, ISR and IDT.
 * No full kernel, writable disk, IRQ workload, or active desktop interaction. */
#include "../kernel/kernel.h"
#ifndef ROUND2_MODE
#define ROUND2_MODE 0
#endif
extern uint32_t kernel_start, kernel_end;
extern void *paging_alloc_heap(size_t size);
extern volatile void *paging_map_mmio(uint32_t phys_addr, size_t size);
static MultibootInfo test_info;
static MultibootMmapEntry test_map[3];
static uint32_t assertions, event_count, allocated[PMM_MAX_FRAMES];

void outb(uint16_t p, uint8_t v) { __asm__ volatile("outb %0, %1" : : "a"(v), "Nd"(p)); }
uint8_t inb(uint16_t p) { uint8_t v; __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(p)); return v; }
void io_wait(void) { outb(0x80, 0); }
void *memset(void *dst, int v, size_t n) { unsigned char *p = dst; while (n--) *p++ = (unsigned char)v; return dst; }
void *memcpy(void *dst, const void *src, size_t n) { unsigned char *d = dst; const unsigned char *s = src; while (n--) *d++ = *s++; return dst; }
void wyrd_append(const char *a, const char *b, const char *c, const char *d, const char *e) {
    (void)a; (void)b; (void)c; (void)d; (void)e; event_count++;
}
void timer_on_irq(void) {}
void task_tick(void) {}
void process_tick(void) {}
void keyboard_on_irq(void) {}
uint32_t syscall_from_registers(Registers *r) { (void)r; return 0; }
void vga_set_color(uint8_t a, uint8_t b) { (void)a; (void)b; }
void vga_putc(char c) { serial_putc(c); }
void vga_write(const char *s) { serial_write(s); }
void vga_write_hex(uint32_t n) {
    vga_write("0x");
    for (int s = 28; s >= 0; s -= 4) vga_putc("0123456789ABCDEF"[(n >> s) & 15]);
}
void vga_write_dec(uint32_t n) {
    char b[11]; unsigned i = 0;
    do { b[i++] = (char)('0' + n % 10); n /= 10; } while (n);
    while (i) vga_putc(b[--i]);
}
static void finish(int ok) {
    vga_write(ok ? "ROUND2 EXIT port=0x00F4 value=0x10\n" : "ROUND2 EXIT port=0x00F4 value=0x11\n");
    outb(0xf4, ok ? 0x10 : 0x11);
    for (;;) __asm__ volatile("cli; hlt");
}
static void check(int ok, const char *name) {
    assertions++;
    if (!ok) {
        vga_write("ROUND2 FAIL "); vga_write(name);
        vga_write(" assertion="); vga_write_dec(assertions); vga_putc('\n'); finish(0);
    }
}
static void pass(const char *name) { vga_write("ROUND2 PASS "); vga_write(name); vga_putc('\n'); }
static uint32_t *directory(void) {
    uint32_t cr3; __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    return (uint32_t *)(uintptr_t)(cr3 & 0xfffff000u);
}
static uint32_t pte(uint32_t va) {
    uint32_t de = directory()[va >> 22];
    if (!(de & 1)) return 0;
    check(!(de & 0x84), "normal supervisor page-table PDE");
    return ((uint32_t *)(uintptr_t)(de & 0xfffff000u))[(va >> 12) & 1023];
}
static void absent(uint32_t va, const char *why) { check(!(pte(va) & 1), why); }
static void mapping(uint32_t va, uint32_t pa, uint32_t flags, uint32_t mask) {
    uint32_t value = pte(va);
    check((value & 0xfffff000u) == pa && (value & mask) == flags, "PTE physical address and attributes");
}
static void setup(void) {
    /* Entire synthetic range is emulator RAM. No low RAM is advertised, so
     * only explicit VGA/kernel mappings survive below the usable interval. */
    test_map[0] = (MultibootMmapEntry){20, 0x100000, 0x7f00000, 1};
    test_map[1] = (MultibootMmapEntry){20, 0x800000, 0x400000, 2};
    test_map[2] = (MultibootMmapEntry){20, 0x1000001, 0x1000, 2};
    test_info.flags = 0x40; test_info.mmap_addr = (uintptr_t)test_map;
    test_info.mmap_length = sizeof(test_map);
    bootinfo_init(MULTIBOOT_BOOTLOADER_MAGIC, (uintptr_t)&test_info);
    pmm_init(); paging_init();
    uint32_t cr0; __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    check(cr0 & 0x80000000u, "hardware paging enabled");
}
static void identity(void) {
    check((uintptr_t)&kernel_start == 0x100000 && (uintptr_t)&kernel_end < 0x800000, "kernel image bounds");
    for (uint32_t va = 0; va < 0x8000000; va += PAGE_SIZE) {
        int expected = va >= 0x100000 && !(va >= 0x800000 && va < 0xc00000)
            && va != 0x1000000 && va != 0x1001000;
        if (va == 0xb8000) expected = 1;
        if (expected) mapping(va, va, 3, 0x87);
        else absent(va, "reserved/unadvertised direct-map hole");
    }
    absent(0x8000000, "128MiB direct-map cap");
    absent(0xa0000000, "arbitrary physical address not identity mapped");
    *(volatile uint16_t *)(uintptr_t)0xb8000 = 0x074f;
    *(volatile uint32_t *)(uintptr_t)0x7fff000 = 0xfeed1234;
    check(*(volatile uint32_t *)(uintptr_t)0x7fff000 == 0xfeed1234, "high identity RAM writable");
    pass("identity RAM, overlapping/unaligned holes, VGA and 128MiB cap");
}
static void heap_test(size_t bytes) {
    uint32_t before = pmm_free_frames();
    check(!paging_alloc_heap(0), "heap zero rejected");
    check(!paging_alloc_heap(0x1000001), "heap limit rejected");
    check(!paging_alloc_heap((size_t)-1), "heap rounding overflow rejected");
    check(!paging_alloc_heap(PAGE_SIZE + 1), "heap non-page size rejected");
    check(pmm_free_frames() == before, "invalid heap sizes no frame leak");
    absent(0xc0000000, "heap initially absent");
    volatile uint8_t *heap = paging_alloc_heap(bytes);
    check((uintptr_t)heap == 0xc0000000, "heap fixed virtual base");
    uint32_t pages = (bytes + PAGE_SIZE - 1) / PAGE_SIZE;
    memset(allocated, 0, sizeof(allocated));
    for (uint32_t i = 0; i < pages; i++) {
        uint32_t entry = pte(0xc0000000 + i * PAGE_SIZE);
        check((entry & 0x87) == 3, "heap supervisor present RW");
        check((entry & 0xfffff000u) >= (uintptr_t)&kernel_end && (entry & 0xfffff000u) < 0x8000000, "heap eligible backing");
        check(!allocated[entry >> 12], "heap physical backing is unique");
        allocated[entry >> 12] = 1;
        *(volatile uint32_t *)(heap + i * PAGE_SIZE) = i ^ 0xa5a5a5a5;
        *(volatile uint32_t *)(heap + i * PAGE_SIZE + PAGE_SIZE - 4) = i ^ 0x5a5a5a5a;
    }
    for (uint32_t i = 0; i < pages; i++) {
        check(*(volatile uint32_t *)(heap + i * PAGE_SIZE) == (i ^ 0xa5a5a5a5), "heap first word retained");
        check(*(volatile uint32_t *)(heap + i * PAGE_SIZE + PAGE_SIZE - 4) == (i ^ 0x5a5a5a5a), "heap pages do not alias");
    }
    absent(0xc0000000 + pages * PAGE_SIZE, "heap exact rounded extent");
    before = pmm_free_frames();
    check(!paging_alloc_heap(PAGE_SIZE), "second heap allocation rejected");
    check(pmm_free_frames() == before, "second heap allocation no leak");
    pass("heap page boundaries, distinct backing, one-success rule");
}
static void mmio_test(void) {
    uint32_t before = pmm_free_frames();
    check(!paging_map_mmio(0x90000000, 0), "MMIO zero rejected");
    check(!paging_map_mmio(0xffffffff, 2), "MMIO physical overflow rejected");
    check(!paging_map_mmio(0x90000001, (size_t)-1), "MMIO size overflow rejected");
    check(!paging_map_mmio(0x200000, 4096), "MMIO RAM alias rejected");
    check(!paging_map_mmio(0xb8000, 4096), "MMIO VGA alias rejected");
    check(pmm_free_frames() == before, "invalid MMIO no frame leak");
    volatile void *mapped = paging_map_mmio(0x90000123, 5000);
    check((uintptr_t)mapped == 0xe0000123, "MMIO preserved offset");
    mapping(0xe0000000, 0x90000000, 0x1b, 0x9f);
    mapping(0xe0001000, 0x90001000, 0x1b, 0x9f);
    absent(0xe0002000, "MMIO exact rounded extent");
    before = pmm_free_frames();
    for (unsigned i = 0; i < 400; i++)
        check(paging_map_mmio(0x90000123, 5000) == mapped, "MMIO repeated reuse");
    check((uintptr_t)paging_map_mmio(0x90000001, 1) == 0xe0000001, "MMIO rounded containment reuse");
    check((uintptr_t)paging_map_mmio(0x90001fff, 1) == 0xe0001fff, "MMIO rounded last byte reuse");
    check(!paging_map_mmio(0x90001000, 8192), "MMIO partial overlap rejected");
    check(pmm_free_frames() == before, "MMIO reuse/conflict no leak");
    check((uintptr_t)paging_map_mmio(0xfffff000, 4096) == 0xe0002000, "MMIO exact 4GiB endpoint accepted");
    mapping(0xe0002000, 0xfffff000, 0x1b, 0x9f);
    pass("MMIO offset, cache bits, aliases, overflow, containment and reuse");
}
static void oom_test(void) {
    uint32_t count = 0, events = event_count;
    while (pmm_free_frames() > 2) {
        uint32_t frame = pmm_alloc_frame();
        check(frame != 0, "drain PMM"); allocated[count++] = frame;
    }
    check(event_count == events, "successful PMM allocations quiet");
    uint32_t before = pmm_free_frames();
    check(!paging_alloc_heap(20 * PAGE_SIZE), "heap OOM reports failure");
    check(pmm_free_frames() == before, "heap OOM frame rollback");
    for (unsigned i = 0; i < 20; i++) absent(0xc0000000 + i * PAGE_SIZE, "heap OOM no partial mapping");
    events = event_count;
    while (count) pmm_free_frame(allocated[--count]);
    check(event_count == events, "successful PMM frees quiet");
    heap_test(20 * PAGE_SIZE); pass("physical exhaustion rollback and retry");
}
static void window_test(void) {
    uint32_t before = pmm_free_frames();
    check(!paging_map_mmio(0x90000000, 0x4000001), "MMIO oversized window rejected");
    check(pmm_free_frames() == before, "MMIO oversized window no leak");
    check((uintptr_t)paging_map_mmio(0x90000000, 0x4000000) == 0xe0000000, "MMIO full 64MiB window");
    for (uint32_t off = 0; off < 0x4000000; off += PAGE_SIZE)
        mapping(0xe0000000 + off, 0x90000000 + off, 0x1b, 0x9f);
    before = pmm_free_frames();
    check(!paging_map_mmio(0xa0000000, 4096), "MMIO exhausted window rejected");
    check(pmm_free_frames() == before, "MMIO exhausted window no leak");
    check((uintptr_t)paging_map_mmio(0x93ffffff, 1) == 0xe3ffffff, "MMIO exhausted contained reuse");
    absent(0xe4000000, "MMIO window hard end");
    pass("MMIO 64MiB capacity, exhaustion and rollback");
}
void kernel_main(uint32_t magic, uint32_t info_addr) {
    (void)info_addr;
    __asm__ volatile("cli"); serial_init(); gdt_init(); idt_init();
    check(magic == MULTIBOOT_BOOTLOADER_MAGIC, "real Multiboot entry magic");
    vga_write("ROUND2 BEGIN mode="); vga_write_dec(ROUND2_MODE); vga_putc('\n');
    check(!paging_alloc_heap(4096), "heap unavailable before paging");
    check(!paging_map_mmio(0x90000000, 4096), "MMIO unavailable before paging");
    if (ROUND2_MODE == 3) {
        vga_write("ROUND2 TRIGGER UD before paging\n"); __asm__ volatile("ud2");
        check(0, "UD must not return");
    }
    setup();
    if (ROUND2_MODE == 1) {
        absent(0x800000, "deliberate hole unmapped");
        vga_write("ROUND2 TRIGGER PF hole\n");
        *(volatile uint32_t *)(uintptr_t)0x800000 = 1;
        check(0, "hole PF must not return");
    } else if (ROUND2_MODE == 2) {
        uint32_t cr4; __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
        cr4 |= 0x10; __asm__ volatile("mov %0, %%cr4" : : "r"(cr4));
        /* With PSE, bit21 is reserved in a 4MiB PDE on Pentium III. */
        directory()[0xd0000000u >> 22] = 0x00200083;
        __asm__ volatile("invlpg (%0)" : : "r"(0xd0000000u) : "memory");
        vga_write("ROUND2 TRIGGER PF reserved\n");
        *(volatile uint32_t *)(uintptr_t)0xd0000000u = 1;
        check(0, "reserved-bit PF must not return");
    } else if (ROUND2_MODE == 4) oom_test();
    else if (ROUND2_MODE == 5) heap_test(0x1000000);
    else if (ROUND2_MODE == 6) window_test();
    else { identity(); heap_test(6 * PAGE_SIZE); mmio_test(); }
    vga_write("ROUND2 PASS all assertions="); vga_write_dec(assertions); vga_putc('\n');
    finish(1);
}

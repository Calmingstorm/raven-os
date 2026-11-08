/* Host harness executes the production parser and allocator, not a model.
 * Fixed low mappings model Multiboot's 32-bit physical pointer ABI. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include "../kernel/kernel.h"

void wyrd_append(const char *a, const char *b, const char *c, const char *d, const char *e) {
    (void)a; (void)b; (void)c; (void)d; (void)e;
}
void vga_set_color(uint8_t a, uint8_t b) { (void)a; (void)b; }
void vga_write(const char *s) { (void)s; }
void vga_write_dec(uint32_t n) { (void)n; }
void vga_write_hex(uint32_t n) { (void)n; }
void vga_putc(char c) { (void)c; }

#include "../kernel/bootinfo.c"
#include "../kernel/pmm.c"

#define ARENA 0x2000000u
static MultibootInfo *info;
static MultibootMmapEntry *map;
static unsigned tests;

static void reset(uint32_t flags) {
    memset((void *)(uintptr_t)ARENA, 0, 0x10000);
    info = (void *)(uintptr_t)ARENA;
    map = (void *)(uintptr_t)(ARENA + 0x1000);
    info->flags = flags;
    info->mmap_addr = ARENA + 0x1000;
    info->mem_lower = 640;
    info->mem_upper = 127 * 1024;
}

static void region(uint64_t base, uint64_t len, uint32_t type) {
    uint32_t i = info->mmap_length / sizeof(*map);
    map[i] = (MultibootMmapEntry){20, base, len, type};
    info->mmap_length += sizeof(*map);
}

static void init(void) {
    bootinfo_init(MULTIBOOT_BOOTLOADER_MAGIC, ARENA);
    pmm_init();
    tests++;
}

static void disabled(void) {
    init();
    assert(pmm_total_frames() == 0);
    assert(pmm_free_frames() == 0);
    assert(pmm_alloc_frame() == 0);
}

static void exhausted(uint32_t expected, uint32_t lo, uint32_t hi) {
    assert(pmm_free_frames() == expected);
    uint32_t n = 0, addr;
    while ((addr = pmm_alloc_frame())) {
        assert(addr >= lo && addr < hi && addr % PAGE_SIZE == 0);
        assert(++n <= expected);
    }
    assert(n == expected && pmm_free_frames() == 0);
}

int main(void) {
    assert(mmap((void *)(uintptr_t)ARENA, 0x10000, PROT_READ | PROT_WRITE,
                MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0) == (void *)(uintptr_t)ARENA);
    assert((uintptr_t)&kernel_start == 0x100000);
    assert((uintptr_t)&kernel_end == 0x180000);

    /* The original regression: only mmap valid (flags 0x40), no basic total. */
    reset(0x40); region(0x100000, 0x300000, 1); init();
    assert(bootinfo_mem_kib() == 3072);
    assert(pmm_total_frames() == 1024);
    exhausted(640, 0x180000, 0x400000);

    /* A basic-memory total must not override mmap holes, and reservations
     * must win in either entry order, including sub-page overlaps. */
    for (unsigned reverse = 0; reverse < 2; reverse++) {
        reset(0x41);
        if (reverse) region(0x280001, 1, 2);
        region(0, 0x200000, 1); region(0x280000, 0x180000, 1);
        if (!reverse) region(0x280001, 1, 2);
        region(0x300000, 0x10000, 3);
        init();
        assert(bootinfo_mem_kib() == (0x200000 / PAGE_SIZE + 0x180000 / PAGE_SIZE - 17) * 4);
        assert(pmm_free_frames() == 495);
        uint32_t n = 0, a;
        while ((a = pmm_alloc_frame())) {
            assert(a >= 0x180000 && a < 0x400000);
            assert(!(a >= 0x200000 && a <= 0x280000));
            assert(!(a >= 0x300000 && a < 0x310000));
            n++;
        }
        assert(n == 495);
        pmm_free_frame(0x100000); pmm_free_frame(0x200000);
        pmm_free_frame(0x280000); pmm_free_frame(0x300000);
        pmm_free_frame(0x180001); pmm_free_frame(0x400000);
        assert(pmm_free_frames() == 0);
        pmm_free_frame(0x180000); pmm_free_frame(0x180000);
        assert(pmm_free_frames() == 1 && pmm_alloc_frame() == 0x180000);
    }

    reset(0x40); region(0x180001, 0x3fff, 1); init();
    exhausted(3, 0x181000, 0x184000);
    reset(0x40); region(0x180000, 0x4000, 1); region(0x180000, 0x4000, 1); init();
    exhausted(4, 0x180000, 0x184000);

    /* Fallback is ONLY when mmap is absent; upper memory starts at 1 MiB. */
    reset(1); info->mem_upper = 3072; init();
    exhausted(640, 0x180000, 0x400000);
    reset(1); info->mem_upper = UINT32_MAX; disabled();
    reset(0); disabled();
    reset(0x41); disabled(); /* present but empty map cannot fall back */
    reset(0x41); region(0x100000, 0x300000, 1); map[0].size = 0; disabled();
    reset(0x41); region(0x100000, 0x300000, 1); map[0].size = 19; disabled();
    reset(0x41); region(0x100000, 0x300000, 1); map[0].size = UINT32_MAX; disabled();
    reset(0x41); region(0x100000, 0x300000, 1); info->mmap_length--; disabled();
    reset(0x41); region(0x100000, 0x300000, 1); info->mmap_length++; disabled();
    reset(0x41); info->mmap_addr = 0; info->mmap_length = 24; disabled();
    reset(0x41); info->mmap_addr = UINT32_MAX - 15; info->mmap_length = 24; disabled();
    reset(0x41); region(UINT64_MAX - 1, 4, 1); disabled();
    reset(0x41); region(0x100000, 0x300000, 1); region(UINT64_MAX - 1, 4, 2); disabled();
    reset(0x40); region(0x180000, 0, 1); init();
    assert(pmm_free_frames() == 0 && bootinfo_mem_kib() == 0);
    reset(0x41); info->mmap_length = 3; disabled();
    reset(0x41); for (unsigned i = 0; i < 257; i++) region(0x180000, 4096, 1); disabled();

    /* Extended records are valid; cursor advances by the supplied size. */
    reset(0x40); region(0x180000, 0x4000, 1); map[0].size = 24; info->mmap_length += 4; init();
    exhausted(4, 0x180000, 0x184000);

    /* Above-cap and above-4GiB regions do not wrap into low memory. */
    reset(0x40); region(0x100000000ULL, 0x100000000ULL, 1); init();
    assert(pmm_total_frames() == 0 && pmm_alloc_frame() == 0);
    reset(0x40); region(0x7fff000, 0x100002000ULL, 1); init();
    exhausted(1, 0x7fff000, 0x8000000);

    /* Metadata, module payloads, module tables and strings stay reserved,
     * even if firmware calls their backing storage usable. */
    reset(0x40 | 8 | 4 | (1u << 9));
    region(ARENA, 0x10000, 1);
    info->mods_count = 1; info->mods_addr = ARENA + 0x2000;
    BootModule *mods = (void *)(uintptr_t)info->mods_addr;
    mods[0] = (BootModule){ARENA + 0x4001, ARENA + 0x6001, ARENA + 0x3000, 0};
    info->cmdline = ARENA + 0x7000;
    ((uint32_t *)(void *)info)[16] = ARENA + 0x8000;
    init();
    assert(pmm_free_frames() == 7);
    uint32_t a, n = 0;
    while ((a = pmm_alloc_frame())) { assert(a >= ARENA + 0x9000 && a < ARENA + 0x10000); n++; }
    assert(n == 7);
    for (uint32_t i = 0; i < 9; i++) pmm_free_frame(ARENA + i * PAGE_SIZE);
    assert(pmm_free_frames() == 0);

    reset(0x48); region(0x180000, 0x4000, 1); info->mods_count = 65; disabled();
    reset(0x48); region(0x180000, 0x4000, 1); info->mods_count = 1; info->mods_addr = UINT32_MAX - 2; disabled();
    reset(0x48); region(0x180000, 0x4000, 1); info->mods_count = 1; info->mods_addr = ARENA + 0x2000;
    mods = (void *)(uintptr_t)info->mods_addr; mods[0].start = 5; mods[0].end = 4; disabled();
    reset(0x44); region(0x180000, 0x4000, 1); info->cmdline = ARENA + 0x2000;
    memset((void *)(uintptr_t)info->cmdline, 'x', MAX_BOOT_STRING); disabled();

    /* Cached parser state survives overwritten boot metadata, as required
     * once paging makes bootloader-owned storage unavailable. */
    reset(0x40); region(0x180000, 0x4000, 1);
    bootinfo_init(MULTIBOOT_BOOTLOADER_MAGIC, ARENA);
    memset((void *)(uintptr_t)ARENA, 0xa5, 0x10000);
    pmm_init(); bootinfo_render(); exhausted(4, 0x180000, 0x184000);

    /* The optional framebuffer tail is reserved even though never read. */
    reset(0x1040); region(ARENA, 0x10000, 1); init();
    assert(pmm_free_frames() == 14); /* info and map pages */

    bootinfo_init(0, ARENA); pmm_init(); assert(pmm_free_frames() == 0);
    bootinfo_init(MULTIBOOT_BOOTLOADER_MAGIC, 0); pmm_init(); assert(pmm_free_frames() == 0);
    bootinfo_init(MULTIBOOT_BOOTLOADER_MAGIC, UINT32_MAX - 5); pmm_init(); assert(pmm_free_frames() == 0);
    printf("memory regression: %u scenarios plus invalid magic/pointers passed\n", tests);
    return 0;
}

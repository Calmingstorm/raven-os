#include "kernel.h"

/* Bound both boot-time work and storage. An incomplete map must never turn
 * into an optimistic contiguous-memory fallback. Capture all data before
 * paging restricts the identity mapping. */
#define MAX_MMAP_ENTRIES 256
#define MAX_MODULES 64
#define MAX_REGIONS 512
#define MAX_BOOT_STRING 4096
#define PHYS_END (1ULL << 32)

typedef struct {
    uint64_t base, len;
    int usable;
} BootRegion;

typedef struct __attribute__((packed)) {
    uint32_t start, end, string, reserved;
} BootModule;

static uint32_t mb_magic, mb_flags, basic_lower, basic_upper;
static uint32_t total_mem_kib, mmap_entries, region_count;
static uint64_t highest_usable;
static int valid, has_basic, has_mmap;
static BootRegion regions[MAX_REGIONS];
static uint8_t usable_pages[PMM_MAX_FRAMES / 8];

/* Count the usable page union below the PMM cap, not the highest physical
 * address (which includes holes) or the optional, often absent bit-0 total.
 * Kernel/low-memory reservations are accounted for separately by the PMM. */
static uint32_t usable_kib(void) {
    memset(usable_pages, 0, sizeof(usable_pages));
    const uint64_t cap = (uint64_t)PMM_MAX_FRAMES * PAGE_SIZE;
    for (int usable = 1; usable >= 0; usable--) {
        for (uint32_t i = 0; i < region_count; i++) {
            const BootRegion *r = &regions[i];
            if (r->usable != usable || r->base >= cap) continue;
            uint64_t end = r->base + r->len;
            if (end > cap) end = cap;
            uint32_t first = (uint32_t)((r->base + (usable ? PAGE_SIZE - 1 : 0)) / PAGE_SIZE);
            uint32_t last = (uint32_t)((end + (usable ? 0 : PAGE_SIZE - 1)) / PAGE_SIZE);
            for (uint32_t page = first; page < last; page++) {
                uint8_t bit = (uint8_t)(1u << (page % 8));
                if (usable) usable_pages[page / 8] |= bit;
                else usable_pages[page / 8] &= (uint8_t)~bit;
            }
        }
    }
    uint32_t count = 0;
    for (uint32_t i = 0; i < PMM_MAX_FRAMES; i++)
        if (usable_pages[i / 8] & (1u << (i % 8))) count++;
    return count * (PAGE_SIZE / 1024);
}

static int add_region(uint64_t base, uint64_t len, int usable) {
    if (len > UINT64_MAX - base || region_count == MAX_REGIONS) return 0;
    if (!len) return 1;
    regions[region_count++] = (BootRegion){base, len, usable};
    if (usable && base + len > highest_usable) highest_usable = base + len;
    return 1;
}

static int physical_span(uint32_t base, uint64_t len) {
    return base && len <= PHYS_END - base;
}

static int reserve_string(uint32_t addr) {
    if (!addr) return 1;
    for (uint32_t i = 0; i < MAX_BOOT_STRING; i++) {
        if ((uint64_t)addr + i >= PHYS_END) return 0;
        if (!*(const char *)(uintptr_t)(addr + i))
            return add_region(addr, i + 1, 0);
    }
    return 0;
}

static int capture_map(const MultibootInfo *info) {
    if (!info->mmap_length || !physical_span(info->mmap_addr, info->mmap_length)) return 0;
    uint32_t offset = 0;
    while (offset < info->mmap_length) {
        uint32_t remaining = info->mmap_length - offset;
        if (remaining < sizeof(uint32_t) || mmap_entries == MAX_MMAP_ENTRIES) return 0;
        const MultibootMmapEntry *entry = (const void *)(uintptr_t)(info->mmap_addr + offset);
        /* Subtract first: neither size + 4 nor the next cursor may wrap. */
        if (entry->size < 20 || entry->size > remaining - sizeof(uint32_t)) return 0;
        if (!add_region(entry->addr, entry->len, entry->type == 1)) return 0;
        mmap_entries++;
        offset += entry->size + sizeof(uint32_t);
    }
    return add_region(info->mmap_addr, info->mmap_length, 0);
}

static int capture_modules(const MultibootInfo *info) {
    if (!(info->flags & (1u << 3)) || !info->mods_count) return 1;
    if (info->mods_count > MAX_MODULES) return 0;
    uint32_t bytes = info->mods_count * sizeof(BootModule);
    if (!physical_span(info->mods_addr, bytes) || !add_region(info->mods_addr, bytes, 0)) return 0;
    const BootModule *modules = (const void *)(uintptr_t)info->mods_addr;
    for (uint32_t i = 0; i < info->mods_count; i++) {
        if (modules[i].end < modules[i].start ||
            !add_region(modules[i].start, (uint64_t)modules[i].end - modules[i].start, 0) ||
            !reserve_string(modules[i].string)) return 0;
    }
    return 1;
}

void bootinfo_init(uint32_t magic, uint32_t info_addr) {
    mb_magic = magic;
    mb_flags = 0;
    basic_lower = basic_upper = total_mem_kib = mmap_entries = region_count = 0;
    highest_usable = 0;
    valid = has_basic = has_mmap = 0;
    /* Traditional Multiboot v1 information is 88 bytes; its framebuffer
     * extension (flag 12) increases this to 116. The public struct is only
     * the prefix through mmap_addr. Never reread bootloader pointers later. */
    if (magic != MULTIBOOT_BOOTLOADER_MAGIC || !physical_span(info_addr, 88)) goto invalid;
    const MultibootInfo *info = (const void *)(uintptr_t)info_addr;
    mb_flags = info->flags;
    has_basic = !!(info->flags & 1u);
    has_mmap = !!(info->flags & (1u << 6));
    if (has_basic) {
        basic_lower = info->mem_lower;
        basic_upper = info->mem_upper;
    }
    if (has_mmap) {
        if (!capture_map(info)) goto invalid;
    } else if (has_basic) {
        /* mem_upper counts KiB above 1 MiB, not above mem_lower. Reject a
         * fallback extending beyond the 32-bit physical address space. */
        uint64_t len = (uint64_t)basic_upper * 1024;
        if (len > PHYS_END - 0x100000 || !add_region(0x100000, len, 1)) goto invalid;
    } else goto invalid;
    uint32_t info_bytes = (info->flags & (1u << 12)) ? 116 : 88;
    if (!physical_span(info_addr, info_bytes) || !add_region(info_addr, info_bytes, 0) ||
        !capture_modules(info)) goto invalid;
    if ((info->flags & (1u << 2)) && !reserve_string(info->cmdline)) goto invalid;
    if (info->flags & (1u << 9)) {
        uint32_t loader_name;
        memcpy(&loader_name, (const void *)(uintptr_t)(info_addr + 64), sizeof(loader_name));
        if (!reserve_string(loader_name)) goto invalid;
    }
    total_mem_kib = usable_kib();
    valid = 1;
    wyrd_append("boot", "inspect", "multiboot", "ok", has_mmap ? "validated memory map captured" : "basic memory fallback captured");
    return;
invalid:
    region_count = 0;
    highest_usable = 0;
    wyrd_append("boot", "inspect", "multiboot", "fail", "invalid boot memory metadata; allocator disabled");
}

void bootinfo_visit_memory(void (*visit)(uint64_t base, uint64_t len, int usable)) {
    if (!valid || !visit) return;
    /* Reserved regions always win, regardless of firmware entry ordering. */
    for (int usable = 1; usable >= 0; usable--)
        for (uint32_t i = 0; i < region_count; i++)
            if (regions[i].usable == usable) visit(regions[i].base, regions[i].len, usable);
}

uint32_t bootinfo_mem_kib(void) { return total_mem_kib; }

static void write_hex64(uint64_t value) {
    vga_write("0x");
    for (int shift = 60; shift >= 0; shift -= 4)
        vga_putc("0123456789ABCDEF"[(value >> shift) & 15]);
}

void bootinfo_render(void) {
    vga_set_color(COLOR_LIGHT_CYAN, COLOR_BLACK);
    vga_write("boot information\n");
    vga_set_color(COLOR_LIGHT_GREY, COLOR_BLACK);
    vga_write("  magic: "); vga_write_hex(mb_magic); vga_putc('\n');
    vga_write("  flags: "); vga_write_hex(mb_flags); vga_putc('\n');
    if (!valid) { vga_write("  memory metadata invalid; allocator disabled\n"); return; }
    vga_write("  memory source: "); vga_write(has_mmap ? "validated mmap\n" : "basic memory fallback\n");
    if (has_basic) {
        vga_write("  mem_lower: "); vga_write_dec(basic_lower); vga_write(" KiB\n");
        vga_write("  mem_upper: "); vga_write_dec(basic_upper); vga_write(" KiB\n");
    }
    vga_write("  mmap:      "); vga_write_dec(mmap_entries); vga_write(" entries\n");
    vga_write("  usable pages below 128 MiB: "); vga_write_dec(total_mem_kib); vga_write(" KiB\n");
    vga_write("  usable_top:"); write_hex64(highest_usable); vga_putc('\n');
    uint32_t shown = 0;
    for (uint32_t i = 0; i < region_count && shown < 8; i++) {
        if (!regions[i].usable) continue;
        vga_write("  usable "); vga_write_dec(shown++);
        vga_write(" base="); write_hex64(regions[i].base);
        vga_write(" len="); write_hex64(regions[i].len); vga_putc('\n');
    }
}

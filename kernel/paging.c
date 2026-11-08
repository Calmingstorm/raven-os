#include "kernel.h"

#define PAGE_PRESENT 0x001u
#define PAGE_RW      0x002u
#define PAGE_PWT     0x008u
#define PAGE_PCD     0x010u
#define ADDRESS_MASK 0xFFFFF000u
#define RAM_TABLES ((PMM_MAX_FRAMES + 1023) / 1024)
#define HEAP_BASE 0xC0000000u
#define HEAP_BYTES (16u * 1024 * 1024)
#define HEAP_TABLES (HEAP_BYTES / (1024 * PAGE_SIZE))
#define MMIO_BASE 0xE0000000u
#define MMIO_BYTES (64u * 1024 * 1024)
#define MMIO_TABLES (MMIO_BYTES / (1024 * PAGE_SIZE))
#define MMIO_DESCRIPTORS 64
#define PHYSICAL_END (1ULL << 32)

extern uint32_t kernel_start, kernel_end;

/* Tables belong to the kernel image, so the PMM reserves them before it
 * allocates anything. Mapping operations cannot run out of table frames. */
static uint32_t page_directory[1024] __attribute__((aligned(4096)));
static uint32_t ram_tables[RAM_TABLES][1024] __attribute__((aligned(4096)));
static uint32_t heap_tables[HEAP_TABLES][1024] __attribute__((aligned(4096)));
static uint32_t mmio_tables[MMIO_TABLES][1024] __attribute__((aligned(4096)));
static uint8_t paging_enabled, heap_allocated, memory_map_present;
static uint32_t identity_pages, heap_pages, mmio_pages, mmio_count;
typedef struct { uint32_t physical, virtual_base, pages; } MmioMapping;
static MmioMapping mmio_mappings[MMIO_DESCRIPTORS];

static void load_page_directory(uint32_t *pd) {
#ifndef ROUND2_PAGING_HOST_TEST
    __asm__ volatile("mov %0, %%cr3" :: "r"(pd) : "memory");
#else
    (void)pd;
#endif
}

static void enable_paging(void) {
#ifndef ROUND2_PAGING_HOST_TEST
    uint32_t cr0;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80000000u;
    __asm__ volatile("mov %0, %%cr0" :: "r"(cr0) : "memory");
#endif
}

/* bootinfo emits usable regions first and reservations last. Whole usable
 * pages only; a reservation touching any byte removes that page. */
static void identity_region(uint64_t base, uint64_t len, int usable) {
    const uint64_t cap = (uint64_t)PMM_MAX_FRAMES * PAGE_SIZE;
    if (!len || len > UINT64_MAX - base) return;
    memory_map_present = 1;
    if (base >= cap) return;
    uint64_t end = base + len;
    if (end > cap) end = cap;
    uint32_t first = (uint32_t)((base + (usable ? PAGE_SIZE - 1 : 0)) / PAGE_SIZE);
    uint32_t last = (uint32_t)((end + (usable ? 0 : PAGE_SIZE - 1)) / PAGE_SIZE);
    for (uint32_t page = first; page < last; page++)
        ram_tables[page / 1024][page % 1024] = usable ? page * PAGE_SIZE | PAGE_PRESENT | PAGE_RW : 0;
}

void paging_init(void) {
    if (paging_enabled) return;
    memory_map_present = 0;
    memset(page_directory, 0, sizeof(page_directory));
    memset(ram_tables, 0, sizeof(ram_tables));
    memset(heap_tables, 0, sizeof(heap_tables));
    memset(mmio_tables, 0, sizeof(mmio_tables));
    bootinfo_visit_memory(identity_region);

    /* Linked BSS contains the bootstrap stack, GDT, IDT and every table.
     * Essential pages remain mapped even if boot metadata is invalid. */
    uint64_t first = (uintptr_t)&kernel_start & ADDRESS_MASK;
    uint64_t end = ((uint64_t)(uintptr_t)&kernel_end + PAGE_SIZE - 1) & ~(uint64_t)(PAGE_SIZE - 1);
    if (end <= first || end > (uint64_t)PMM_MAX_FRAMES * PAGE_SIZE) {
        serial_write("paging: invalid kernel physical extent; paging disabled\n");
        return;
    }
    for (uint32_t page = (uint32_t)(first / PAGE_SIZE); page < end / PAGE_SIZE; page++)
        ram_tables[page / 1024][page % 1024] = page * PAGE_SIZE | PAGE_PRESENT | PAGE_RW;
    ram_tables[0][0xB8] = 0xB8000u | PAGE_PRESENT | PAGE_RW | PAGE_PCD | PAGE_PWT;
    identity_pages = 0;
    for (uint32_t table = 0; table < RAM_TABLES; table++) {
        int present = 0;
        for (uint32_t page = 0; page < 1024; page++) {
            if (ram_tables[table][page] & PAGE_PRESENT) { identity_pages++; present = 1; }
        }
        if (present) page_directory[table] = (uint32_t)(uintptr_t)ram_tables[table] | PAGE_PRESENT | PAGE_RW;
    }
    for (uint32_t i = 0; i < HEAP_TABLES; i++)
        page_directory[HEAP_BASE / (1024 * PAGE_SIZE) + i] = (uint32_t)(uintptr_t)heap_tables[i] | PAGE_PRESENT | PAGE_RW;
    for (uint32_t i = 0; i < MMIO_TABLES; i++)
        page_directory[MMIO_BASE / (1024 * PAGE_SIZE) + i] = (uint32_t)(uintptr_t)mmio_tables[i] | PAGE_PRESENT | PAGE_RW;
    load_page_directory(page_directory);
    enable_paging();
    paging_enabled = 1;
    wyrd_append("memory", "init", "paging", "ok", "validated RAM identity map; isolated heap and MMIO windows");
}

void *paging_alloc_heap(size_t bytes) {
    if (!paging_enabled || heap_allocated || !bytes || bytes > HEAP_BYTES || bytes % PAGE_SIZE) return NULL;
    uint32_t pages = (uint32_t)(bytes / PAGE_SIZE), acquired = 0;
    for (; acquired < pages; acquired++) {
        uint32_t frame = pmm_alloc_frame();
        if (!frame) break;
        uint32_t page = frame / PAGE_SIZE;
        if (page >= PMM_MAX_FRAMES || !(ram_tables[page / 1024][page % 1024] & PAGE_PRESENT)) {
            pmm_free_frame(frame);
            break;
        }
        /* Staged, not present until every independent frame is obtained. */
        heap_tables[acquired / 1024][acquired % 1024] = frame | PAGE_RW;
    }
    if (acquired != pages) {
        while (acquired) {
            acquired--;
            uint32_t *entry = &heap_tables[acquired / 1024][acquired % 1024];
            pmm_free_frame(*entry & ADDRESS_MASK);
            *entry = 0;
        }
        return NULL;
    }
    for (uint32_t i = 0; i < pages; i++) heap_tables[i / 1024][i % 1024] |= PAGE_PRESENT;
    load_page_directory(page_directory);
    heap_pages = pages;
    heap_allocated = 1;
    return (void *)(uintptr_t)HEAP_BASE;
}

/* MMIO may not alias reported ordinary RAM, even above our direct-map cap
 * or reserved by the bootloader. Use captured map, never boot pointers. */
static uint64_t request_start, request_end;
static int request_conflict;
static void check_ram_alias(uint64_t base, uint64_t len, int usable) {
    if (!usable || !len || len > UINT64_MAX - base) return;
    if (base < request_end && request_start < base + len) request_conflict = 1;
}

volatile void *paging_map_mmio(uint32_t phys_addr, size_t size) {
    /* With invalid boot metadata we cannot rule out a cached RAM alias. */
    if (!paging_enabled || !memory_map_present || !size || (uint64_t)size > PHYSICAL_END - phys_addr) return NULL;
    uint64_t start = (uint64_t)phys_addr & ADDRESS_MASK;
    uint64_t end = ((uint64_t)phys_addr + size + PAGE_SIZE - 1) & ~(uint64_t)(PAGE_SIZE - 1);
    uint32_t offset = phys_addr & (PAGE_SIZE - 1);
    for (uint32_t i = 0; i < mmio_count; i++) {
        const MmioMapping *m = &mmio_mappings[i];
        uint64_t old_end = (uint64_t)m->physical + (uint64_t)m->pages * PAGE_SIZE;
        if (start >= m->physical && end <= old_end)
            return (volatile void *)(uintptr_t)(m->virtual_base + (uint32_t)(start - m->physical) + offset);
        if (start < old_end && m->physical < end) return NULL;
    }
    uint64_t pages64 = (end - start) / PAGE_SIZE;
    if (mmio_count == MMIO_DESCRIPTORS || pages64 > MMIO_BYTES / PAGE_SIZE - mmio_pages) return NULL;
    request_start = start;
    request_end = end;
    request_conflict = 0;
    bootinfo_visit_memory(check_ram_alias);
    if (request_conflict) return NULL;
    uint64_t direct_end = (uint64_t)PMM_MAX_FRAMES * PAGE_SIZE;
    if (end < direct_end) direct_end = end;
    for (uint32_t page = (uint32_t)(start / PAGE_SIZE); (uint64_t)page * PAGE_SIZE < direct_end; page++)
        if (ram_tables[page / 1024][page % 1024] & PAGE_PRESENT) return NULL;

    /* All checks precede the first write. Commit cannot fail: descriptor
     * and all page tables are statically reserved. */
    uint32_t pages = (uint32_t)pages64;
    uint32_t virtual_base = MMIO_BASE + mmio_pages * PAGE_SIZE;
    for (uint32_t i = 0; i < pages; i++) {
        uint32_t slot = mmio_pages + i;
        mmio_tables[slot / 1024][slot % 1024] = (uint32_t)(start + (uint64_t)i * PAGE_SIZE) |
            PAGE_PRESENT | PAGE_RW | PAGE_PCD | PAGE_PWT;
    }
    mmio_mappings[mmio_count++] = (MmioMapping){(uint32_t)start, virtual_base, pages};
    mmio_pages += pages;
    load_page_directory(page_directory);
    return (volatile void *)(uintptr_t)(virtual_base + offset);
}

void paging_render(void) {
    vga_set_color(COLOR_LIGHT_CYAN, COLOR_BLACK);
    vga_write("paging\n");
    vga_set_color(COLOR_LIGHT_GREY, COLOR_BLACK);
    vga_write("  enabled: "); vga_write_dec(paging_enabled); vga_putc('\n');
    vga_write("  directory: "); vga_write_hex((uint32_t)(uintptr_t)page_directory); vga_putc('\n');
    vga_write("  identity: validated RAM below 128MiB, holes absent; kernel + VGA\n");
    vga_write("  identity pages: "); vga_write_dec(identity_pages); vga_putc('\n');
    vga_write("  heap pages: "); vga_write_dec(heap_pages); vga_write(" at 0xC0000000\n");
    vga_write("  MMIO pages: "); vga_write_dec(mmio_pages); vga_write(" at 0xE0000000\n");
    vga_write("  note: supervisor-only maps, not process isolation yet\n");
}

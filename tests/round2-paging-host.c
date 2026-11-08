/* Exercise production paging/PMM logic without privileged instructions.
 * The emulator suite separately executes real CR0/CR3 and exception paths. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define ROUND2_PAGING_HOST_TEST
#include "../kernel/pmm.c"
#include "../kernel/paging.c"

typedef struct { uint64_t base, len; int usable; } TestRegion;
static TestRegion regions[8];
static unsigned region_count, events;
static uint32_t held[PMM_MAX_FRAMES];

void bootinfo_visit_memory(void (*visit)(uint64_t, uint64_t, int)) {
    for (int usable = 1; usable >= 0; usable--)
        for (unsigned i = 0; i < region_count; i++)
            if (regions[i].usable == usable) visit(regions[i].base, regions[i].len, usable);
}
void wyrd_append(const char *a, const char *b, const char *c, const char *d, const char *e) {
    (void)a; (void)b; (void)c; (void)d; (void)e; events++;
}
void serial_write(const char *s) { (void)s; }

static void reset(void) {
    regions[0] = (TestRegion){0, 128u * 1024 * 1024, 1};
    regions[1] = (TestRegion){8u * 1024 * 1024, 4u * 1024 * 1024, 0};
    regions[2] = (TestRegion){16u * 1024 * 1024 + 1, PAGE_SIZE, 0};
    regions[3] = (TestRegion){0x90000001u, 1, 1}; /* above the direct map */
    region_count = 4;
    paging_enabled = heap_allocated = 0;
    heap_pages = mmio_pages = mmio_count = 0;
    memset(mmio_mappings, 0, sizeof(mmio_mappings));
    pmm_init(); paging_init(); events = 0;
}

static void identity_test(void) {
    reset();
    for (uint32_t page = 0; page < PMM_MAX_FRAMES; page++) {
        uint32_t pte = ram_tables[page / 1024][page % 1024];
        int hole = (page >= 2048 && page < 3072) || page == 4096 || page == 4097;
        assert(!!(pte & PAGE_PRESENT) == !hole);
        if (!hole) {
            assert((pte & ADDRESS_MASK) == page * PAGE_SIZE);
            assert((pte & 7) == 3); /* supervisor, writable */
        }
    }
    assert((ram_tables[0][0xB8] & 0x9F) == 0x1B);
    assert(!page_directory[RAM_TABLES]);
    assert(!paging_alloc_heap(0));
    assert(!paging_alloc_heap(1));
    assert(!paging_alloc_heap(HEAP_BYTES + PAGE_SIZE));
    assert(!paging_alloc_heap(SIZE_MAX));
    puts("PASS validated RAM cap, reserved/unaligned holes, VGA attributes, heap input checks");

    /* Invalid/missing boot metadata must not recreate the old optimistic
     * four-MiB identity map. Only essential image and VGA pages survive. */
    paging_enabled = 0; region_count = 0;
    pmm_init(); paging_init();
    assert(!pmm_free_frames() && !pmm_alloc_frame());
    for (uint32_t page = 0; page < PMM_MAX_FRAMES; page++) {
        int essential = (page >= 256 && page < 384) || page == 0xB8;
        assert(!!(ram_tables[page / 1024][page % 1024] & PAGE_PRESENT) == essential);
    }
    assert(!paging_alloc_heap(PAGE_SIZE));
    assert(!paging_map_mmio(0xA0000000u, PAGE_SIZE));
    puts("PASS absent validated RAM leaves only kernel/VGA identity mappings; allocator closed");
}

static void heap_test(void) {
    reset();
    uint32_t free_before = pmm_free_frames();
    /* Keep alternating frames allocated to prove physically fragmented RAM
     * still supplies a contiguous virtual arena. */
    for (unsigned i = 0; i < 8192; i++) held[i] = pmm_alloc_frame();
    for (unsigned i = 0; i < 8192; i += 2) pmm_free_frame(held[i]);
    free_before -= 4096;
    assert(!events);
    assert(paging_alloc_heap(HEAP_BYTES) == (void *)(uintptr_t)HEAP_BASE);
    assert(pmm_free_frames() == free_before - HEAP_BYTES / PAGE_SIZE);
    assert(!events); /* 12,288 successes do not evict timeline entries */
    uint8_t seen[PMM_MAX_FRAMES / 8] = {0};
    for (uint32_t i = 0; i < HEAP_BYTES / PAGE_SIZE; i++) {
        uint32_t pte = heap_tables[i / 1024][i % 1024];
        uint32_t frame = (pte & ADDRESS_MASK) / PAGE_SIZE;
        assert((pte & 0x9F) == 3);
        assert(!(seen[frame / 8] & (1u << (frame % 8))));
        seen[frame / 8] |= (uint8_t)(1u << (frame % 8));
        assert(eligible[frame / 8] & (1u << (frame % 8)));
    }
    free_before = pmm_free_frames();
    assert(!paging_alloc_heap(PAGE_SIZE));
    assert(pmm_free_frames() == free_before);
    puts("PASS 16MiB independent-frame heap, quiet PMM, one-arena rule");

    reset();
    while (pmm_free_frames() > 17) (void)pmm_alloc_frame();
    assert(!events);
    assert(!paging_alloc_heap(18 * PAGE_SIZE));
    assert(pmm_free_frames() == 17);
    for (uint32_t i = 0; i < HEAP_BYTES / PAGE_SIZE; i++) assert(!heap_tables[i / 1024][i % 1024]);
    assert(!heap_allocated && !heap_pages);
    assert(events == 1); /* aggregate OOM only */
    assert(paging_alloc_heap(17 * PAGE_SIZE) == (void *)(uintptr_t)HEAP_BASE);
    assert(!pmm_free_frames());
    unsigned after_first_oom;
    assert(!pmm_alloc_frame()); after_first_oom = events;
    assert(!pmm_alloc_frame() && events == after_first_oom);
    puts("PASS heap OOM rollback, no consumed VA/frames, retry, bounded OOM diagnostics");
}

static void assert_empty_mmio(void) {
    assert(!mmio_count && !mmio_pages);
    for (unsigned i = 0; i < MMIO_BYTES / PAGE_SIZE; i++) assert(!mmio_tables[i / 1024][i % 1024]);
}

static void mmio_test(void) {
    reset();
    uint32_t free_before = pmm_free_frames();
    assert(!paging_map_mmio(0xA0000000u, 0));
    assert(!paging_map_mmio(0xFFFFFFFFu, 2));
    assert(!paging_map_mmio(0xA0000000u, SIZE_MAX));
    assert(!paging_map_mmio(0xA0000000u, 256u * 1024 * 1024));
    assert(!paging_map_mmio(0x100000, PAGE_SIZE));
    assert(!paging_map_mmio(0xB8000, 1));
    assert(!paging_map_mmio(16u * 1024 * 1024, 1)); /* reserved but ordinary RAM */
    assert(!paging_map_mmio(0x90000000u, 1)); /* outward rounding touches RAM */
    assert_empty_mmio();
    volatile void *mapping = paging_map_mmio(0xA0000123u, 5000);
    assert(mapping == (volatile void *)(uintptr_t)(MMIO_BASE + 0x123));
    assert(mmio_pages == 2 && mmio_count == 1);
    assert(mmio_tables[0][0] == (0xA0000000u | 0x1B));
    assert(mmio_tables[0][1] == (0xA0001000u | 0x1B));
    for (unsigned i = 0; i < 1000; i++) {
        assert(paging_map_mmio(0xA0000123u, 5000) == mapping);
        assert(paging_map_mmio(0xA0001001u, 1) == (volatile void *)(uintptr_t)(MMIO_BASE + 0x1001));
    }
    assert(!paging_map_mmio(0x9FFFF000u, 8192));
    assert(!paging_map_mmio(0xA0001000u, 8192));
    assert(mmio_pages == 2 && mmio_count == 1);
    assert(paging_map_mmio(0xFFFFFFFFu, 1) == (volatile void *)(uintptr_t)(MMIO_BASE + 3 * PAGE_SIZE - 1));
    assert(mmio_tables[0][2] == (0xFFFFF000u | 0x1B));
    assert(paging_map_mmio(0xFFFFF000u, PAGE_SIZE) == (volatile void *)(uintptr_t)(MMIO_BASE + 2 * PAGE_SIZE));
    assert(pmm_free_frames() == free_before && !events);
    puts("PASS MMIO checked rounding including final physical page, flags, reuse, overlap and RAM alias rejection");

    reset();
    for (unsigned i = 0; i < MMIO_DESCRIPTORS; i++)
        assert(paging_map_mmio(0xA0000000u + i * 2 * PAGE_SIZE, 1));
    assert(mmio_count == MMIO_DESCRIPTORS);
    assert(!paging_map_mmio(0xB0000000u, 1));
    assert(mmio_count == MMIO_DESCRIPTORS && mmio_pages == MMIO_DESCRIPTORS);
    assert(paging_map_mmio(0xA0000000u, 1) == (volatile void *)(uintptr_t)MMIO_BASE);
    reset();
    assert(paging_map_mmio(0xA0000000u, MMIO_BYTES) == (volatile void *)(uintptr_t)MMIO_BASE);
    assert(!paging_map_mmio(0xB0000000u, 1));
    assert(mmio_pages == MMIO_BYTES / PAGE_SIZE && mmio_count == 1);
    assert(paging_map_mmio(0xA3FFFFFFu, 1) == (volatile void *)(uintptr_t)(MMIO_BASE + MMIO_BYTES - 1));
    puts("PASS MMIO descriptor/window exhaustion transactional; covered reuse still works");

    reset();
    paging_enabled = 0;
    assert(!paging_alloc_heap(PAGE_SIZE));
    assert(!paging_map_mmio(0xA0000000u, PAGE_SIZE));
    assert_empty_mmio();
    puts("PASS unavailable paging rejects maps without state changes");
}

int main(void) {
    identity_test(); heap_test(); mmio_test();
    puts("round2 paging host regression: PASS");
    return 0;
}

#include "kernel.h"

#define HEAP_MAX_BYTES (16u * 1024u * 1024u)
#define HEAP_ALIGNMENT 16u
#define HEAP_GUARD 0x48454150u

/* Headers and payloads live inside the arena; no user-supplied links to trust.
 * Single-context allocator: do not call from interrupt handlers. */
typedef struct HeapBlock {
    uint32_t size;
    uint32_t requested;
    uint32_t used;
    uint32_t guard;
} HeapBlock;

_Static_assert(sizeof(HeapBlock) == HEAP_ALIGNMENT, "heap header alignment");

static uint8_t *heap_base;
static uint32_t heap_bytes;
static uint32_t heap_rejected;

static uint32_t block_guard(const HeapBlock *b) {
    return HEAP_GUARD ^ (uint32_t)((const uint8_t *)b - heap_base)
        ^ b->size ^ b->requested ^ b->used;
}

static void block_write(HeapBlock *b, uint32_t size, uint32_t requested, uint32_t used) {
    b->size = size;
    b->requested = requested;
    b->used = used;
    b->guard = block_guard(b);
}

static int block_valid(const HeapBlock *b) {
    uintptr_t addr = (uintptr_t)b, base = (uintptr_t)heap_base;
    if (!heap_base || heap_bytes < sizeof(*b) + HEAP_ALIGNMENT || addr < base
        || addr - base > heap_bytes - sizeof(*b)
        || (addr - base) % HEAP_ALIGNMENT) return 0;
    if (b->size < HEAP_ALIGNMENT || b->size % HEAP_ALIGNMENT
        || b->size > heap_bytes - (uint32_t)(addr - base) - sizeof(*b)
        || b->used > 1 || b->requested > b->size
        || (b->used ? !b->requested : b->requested != 0)) return 0;
    return b->guard == block_guard(b);
}

/* Validate the current header before calling, and the result before reading. */
static HeapBlock *block_next(HeapBlock *b) {
    uint8_t *next = (uint8_t *)(b + 1) + b->size;
    return next == heap_base + heap_bytes ? 0 : (HeapBlock *)next;
}

static void reset_arena(void) {
    block_write((HeapBlock *)heap_base, heap_bytes - sizeof(HeapBlock), 0, 0);
}

void heap_init(void) {
    if (heap_base) return; /* Paging owns one successful arena; never lose it. */
    heap_rejected = 0;
    for (uint32_t bytes = HEAP_MAX_BYTES; bytes >= PAGE_SIZE; bytes >>= 1) {
        void *arena = paging_alloc_heap(bytes);
        if (!arena) continue;
        heap_base = arena;
        heap_bytes = bytes;
        reset_arena();
        wyrd_append("memory", "init", "kernel-heap",
            bytes == HEAP_MAX_BYTES ? "ok" : "degraded",
            bytes == HEAP_MAX_BYTES ? "16 MiB virtual heap online" :
            bytes >= 64u * 1024u ? "reduced heap: low physical memory" :
            "emergency heap below 64 KiB: large allocations unavailable");
        return;
    }
    heap_bytes = 0;
    wyrd_append("memory", "init", "kernel-heap", "fail", "no heap arena: allocations disabled");
}

static int allocation_size(size_t size, uint32_t *need) {
    if (!size || !heap_base || size > heap_bytes - sizeof(HeapBlock)) return 0;
    /* Bound before narrowing or adding the alignment bias. */
    *need = ((uint32_t)size + HEAP_ALIGNMENT - 1) & ~(HEAP_ALIGNMENT - 1);
    return *need <= heap_bytes - sizeof(HeapBlock);
}

static void merge_right(HeapBlock *b) {
    HeapBlock *next;
    while ((next = block_next(b)) && block_valid(next) && !next->used)
        block_write(b, b->size + sizeof(*b) + next->size, b->requested, b->used);
}

static void split_block(HeapBlock *b, uint32_t need) {
    if (b->size - need < sizeof(*b) + HEAP_ALIGNMENT) return;
    uint32_t rest = b->size - need - sizeof(*b);
    block_write(b, need, b->requested, b->used);
    HeapBlock *tail = (HeapBlock *)((uint8_t *)(b + 1) + need);
    block_write(tail, rest, 0, 0);
    merge_right(tail);
}

void *kmalloc(size_t size) {
    uint32_t need;
    if (!allocation_size(size, &need)) return 0;
    for (HeapBlock *b = (HeapBlock *)heap_base; b; b = block_next(b)) {
        if (!block_valid(b)) return 0;
        if (!b->used && b->size >= need) {
            block_write(b, b->size, (uint32_t)size, 1);
            split_block(b, need);
            return b + 1;
        }
    }
    return 0;
}

static HeapBlock *find_allocated(void *ptr, HeapBlock **previous) {
    uintptr_t addr = (uintptr_t)ptr, base = (uintptr_t)heap_base;
    *previous = 0;
    if (!heap_base || addr < base + sizeof(HeapBlock) || addr >= base + heap_bytes
        || addr % HEAP_ALIGNMENT) return 0;
    for (HeapBlock *b = (HeapBlock *)heap_base; b; b = block_next(b)) {
        if (!block_valid(b)) return 0;
        if ((void *)(b + 1) == ptr) return b->used ? b : 0;
        if ((uintptr_t)(b + 1) > addr) return 0;
        *previous = b;
    }
    return 0;
}

void kfree(void *ptr) {
    if (!ptr) return;
    HeapBlock *previous;
    HeapBlock *b = find_allocated(ptr, &previous);
    if (!b) { if (heap_rejected != UINT32_MAX) heap_rejected++; return; }
    block_write(b, b->size, 0, 0);
    merge_right(b);
    if (previous && !previous->used) merge_right(previous);
}

void *krealloc(void *ptr, size_t size) {
    if (!ptr) return kmalloc(size);
    if (!size) { kfree(ptr); return 0; }
    HeapBlock *previous;
    HeapBlock *b = find_allocated(ptr, &previous);
    uint32_t need;
    if (!b || !allocation_size(size, &need)) return 0;
    if (need <= b->size) {
        block_write(b, b->size, (uint32_t)size, 1);
        split_block(b, need);
        return ptr;
    }
    HeapBlock *next = block_next(b);
    if (next && block_valid(next) && !next->used
        && b->size + sizeof(*b) + next->size >= need) {
        block_write(b, b->size + sizeof(*b) + next->size, (uint32_t)size, 1);
        split_block(b, need);
        return ptr;
    }
    void *replacement = kmalloc(size);
    if (!replacement) return 0; /* Original allocation and contents survive. */
    memcpy(replacement, ptr, b->requested < size ? b->requested : size);
    kfree(ptr);
    return replacement;
}

void *kcalloc(size_t count, size_t size) {
    if (!count || !size || count > SIZE_MAX / size) return 0;
    size_t bytes = count * size;
    void *ptr = kmalloc(bytes);
    if (ptr) memset(ptr, 0, bytes);
    return ptr;
}

void kfree_all(void) {
    /* Legacy reset invalidates all earlier pointers. A stale pointer cannot be
     * distinguished once a new allocation reuses that same address. */
    if (heap_base) reset_arena();
    wyrd_append("heap", "free", "all", "ok", "heap reset: all previous allocations invalidated");
}

void heap_render(void) {
    uint32_t requested = 0, reserved = 0, free_bytes = 0, largest = 0;
    uint32_t blocks = 0, allocated = 0, valid = 1;
    for (HeapBlock *b = (HeapBlock *)heap_base; b; b = block_next(b)) {
        if (!block_valid(b)) { valid = 0; break; }
        blocks++;
        if (b->used) { allocated++; requested += b->requested; reserved += b->size; }
        else { free_bytes += b->size; if (b->size > largest) largest = b->size; }
    }
    vga_set_color(COLOR_LIGHT_CYAN, COLOR_BLACK);
    vga_write("kernel heap\n");
    vga_set_color(COLOR_LIGHT_GREY, COLOR_BLACK);
    vga_write("  base: "); vga_write_hex((uint32_t)(uintptr_t)heap_base);
    vga_write("  size: "); vga_write_dec(heap_bytes); vga_write(" bytes\n");
    vga_write("  allocated: "); vga_write_dec(allocated);
    vga_write("  requested: "); vga_write_dec(requested);
    vga_write("  reserved: "); vga_write_dec(reserved); vga_putc('\n');
    vga_write("  free: "); vga_write_dec(free_bytes);
    vga_write("  largest: "); vga_write_dec(largest); vga_putc('\n');
    vga_write("  blocks: "); vga_write_dec(blocks);
    vga_write("  metadata: "); vga_write_dec(blocks * sizeof(HeapBlock));
    vga_write("  rejected frees: "); vga_write_dec(heap_rejected); vga_putc('\n');
    if (!valid) vga_write("  CORRUPT metadata: allocation traversal stopped\n");
}

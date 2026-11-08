#define _GNU_SOURCE
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

/* Real implementation, not a model. Including permits structural invariants
 * and isolated simulated boots without a production test/reset API. */
#include "../kernel/heap.c"

static uint8_t *mapping, *arena;
static uint32_t available, attempts, successes, events;
static size_t requests[32];
static char last_result[32], last_note[160], output[4096];
static size_t output_len;

void *paging_alloc_heap(size_t bytes) {
    assert(attempts < 32);
    requests[attempts++] = bytes;
    assert(bytes && bytes <= HEAP_MAX_BYTES && bytes % PAGE_SIZE == 0);
    if (bytes > available || successes) return NULL;
    successes++;
    return arena;
}

void wyrd_append(const char *actor, const char *verb, const char *object,
                 const char *result, const char *note) {
    (void)actor; (void)verb; (void)object;
    events++;
    snprintf(last_result, sizeof(last_result), "%s", result);
    snprintf(last_note, sizeof(last_note), "%s", note);
}
void vga_set_color(uint8_t fg, uint8_t bg) { (void)fg; (void)bg; }
void vga_putc(char ch) {
    assert(output_len + 1 < sizeof(output));
    output[output_len++] = ch;
    output[output_len] = 0;
}
void vga_write(const char *s) { while (*s) vga_putc(*s++); }
void vga_write_dec(uint32_t n) { char s[16]; snprintf(s, sizeof(s), "%u", n); vga_write(s); }
void vga_write_hex(uint32_t n) { char s[16]; snprintf(s, sizeof(s), "%x", n); vga_write(s); }

static void boot(uint32_t capacity) {
    if (mapping) assert(munmap(mapping, HEAP_MAX_BYTES + 2 * PAGE_SIZE) == 0);
    mapping = mmap(NULL, HEAP_MAX_BYTES + 2 * PAGE_SIZE, PROT_NONE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    assert(mapping != MAP_FAILED);
    arena = mapping + PAGE_SIZE;
    assert((uintptr_t)arena % PAGE_SIZE == 0);
    if (capacity) assert(mprotect(arena, capacity, PROT_READ | PROT_WRITE) == 0);
    heap_base = NULL; heap_bytes = 0; heap_rejected = 0;
    available = capacity; attempts = successes = events = 0;
    heap_init();
    assert(events == 1);
}

static unsigned check_heap(void) {
    uint32_t total = 0, count = 0;
    int prior_free = 0;
    for (HeapBlock *b = (HeapBlock *)heap_base; b; b = block_next(b)) {
        assert(block_valid(b));
        assert((uintptr_t)(b + 1) % 16 == 0);
        assert(!prior_free || b->used); /* Coalescing must be immediate. */
        prior_free = !b->used;
        total += sizeof(*b) + b->size;
        count++;
        assert(total <= heap_bytes && count <= heap_bytes / 32);
    }
    assert(total == heap_bytes);
    return count;
}

static void clean(void) {
    assert(check_heap() == 1);
    HeapBlock *b = (HeapBlock *)heap_base;
    assert(!b->used && b->requested == 0 && b->size == heap_bytes - 16);
    void *all = kmalloc(heap_bytes - 16);
    assert(all == heap_base + 16 && !kmalloc(1));
    memset(all, 0xb6, heap_bytes - 16);
    kfree(all);
    assert(check_heap() == 1);
}

static void verify_bytes(const uint8_t *p, size_t n, uint8_t byte) {
    for (size_t i = 0; i < n; i++) assert(p[i] == byte);
}

static void test_low_memory(void) {
    for (uint32_t cap = HEAP_MAX_BYTES; cap >= PAGE_SIZE; cap >>= 1) {
        boot(cap);
        assert(heap_bytes == cap && successes == 1);
        for (unsigned i = 0; i < attempts; i++) assert(requests[i] == HEAP_MAX_BYTES >> i);
        assert(!strcmp(last_result, cap == HEAP_MAX_BYTES ? "ok" : "degraded"));
        if (cap < 65536) assert(strstr(last_note, "emergency"));
        else if (cap != HEAP_MAX_BYTES) assert(strstr(last_note, "low physical memory"));
        unsigned old_attempts = attempts;
        void *p = kmalloc(33); assert(p); memset(p, 19, 33);
        heap_init(); /* Reinitialization may not throw away live allocations. */
        assert(attempts == old_attempts); verify_bytes(p, 33, 19); kfree(p);
        clean();
    }
    boot(0);
    assert(attempts == 13 && !successes && !heap_base && !heap_bytes);
    assert(!strcmp(last_result, "fail"));
    assert(!kmalloc(1) && !kcalloc(2, 3) && !krealloc(NULL, 1));
    kfree(NULL); kfree((void *)(uintptr_t)16); kfree_all(); heap_render();
}

static void test_large_and_boundaries(void) {
    boot(HEAP_MAX_BYTES);
    uint8_t *large = kmalloc(6u * 1024u * 1024u);
    uint8_t *headroom = kmalloc(2u * 1024u * 1024u);
    assert(large && headroom);
    for (size_t i = 0; i < 6u * 1024u * 1024u; i++) large[i] = (uint8_t)(i ^ (i >> 12));
    memset(headroom, 0x75, 2u * 1024u * 1024u);
    for (size_t i = 0; i < 6u * 1024u * 1024u; i++) assert(large[i] == (uint8_t)(i ^ (i >> 12)));
    verify_bytes(headroom, 2u * 1024u * 1024u, 0x75);
    assert(events == 1); /* No per-allocation success/failure timeline spam. */
    kfree(large); kfree(headroom); clean();
    assert(!kmalloc(0) && !kmalloc(SIZE_MAX) && !kmalloc(UINT32_MAX));
    assert(!kmalloc(HEAP_MAX_BYTES) && !kmalloc(HEAP_MAX_BYTES - 15));
    assert(!kcalloc(0, 1) && !kcalloc(1, 0));
    assert(!kcalloc(SIZE_MAX, 2) && !kcalloc(SIZE_MAX / 16 + 1, 16));
    assert(!kcalloc(1, HEAP_MAX_BYTES));
    for (unsigned n = 1; n < 128; n++) {
        uint8_t *p = kmalloc(n); assert(p && (uintptr_t)p % 16 == 0);
        memset(p, 0x5a, n); kfree(p);
    }
    uint8_t *p = kmalloc(2049); assert(p); memset(p, 0xff, 2049); kfree(p);
    p = kcalloc(683, 3); assert(p); verify_bytes(p, 2049, 0); kfree(p);
    /* Both split boundaries: < header+16 leaves internal slack; == splits. */
    p = kmalloc(HEAP_MAX_BYTES - 32); assert(p); assert(check_heap() == 1); kfree(p);
    p = kmalloc(HEAP_MAX_BYTES - 48); assert(p); assert(check_heap() == 2);
    void *tiny = kmalloc(16); assert(tiny && !kmalloc(1)); kfree(p); kfree(tiny); clean();
    output_len = 0; heap_render();
    assert(strstr(output, "size: 16777216 bytes"));
    assert(strstr(output, "blocks: 1  metadata: 16"));
    assert(output_len < 512);
}

static void test_invalid_and_fragmentation(void) {
    boot(HEAP_MAX_BYTES);
    uint8_t *a = kmalloc(64), *b = kmalloc(64), *c = kmalloc(64);
    assert(a && b && c); memset(a, 1, 64); memset(b, 2, 64); memset(c, 3, 64);
    kfree(NULL); kfree((void *)(uintptr_t)1); kfree(arena); kfree(arena - 16);
    kfree(arena + HEAP_MAX_BYTES); kfree(a + 1); kfree(a + 16); kfree(b - 16);
    assert(!krealloc(c + 16, 32));
    verify_bytes(a, 64, 1); verify_bytes(b, 64, 2); verify_bytes(c, 64, 3);
    assert(check_heap() == 4);
    kfree(b); kfree(b); assert(check_heap() == 4);
    kfree(a); assert(check_heap() == 3);
    kfree(b); /* Stale header absorbed into a must not become a real block. */
    assert(check_heap() == 3);
    void *joined = kmalloc(144); assert(joined == a); kfree(joined); kfree(c); clean();
    a = kmalloc(128); b = kmalloc(128); assert(a && b);
    kfree_all(); assert(check_heap() == 1);
    kfree(b); /* Reset has not reused this stale interior pointer. */
    clean();
}

static void test_realloc(void) {
    boot(HEAP_MAX_BYTES);
    assert(!krealloc(NULL, 0));
    uint8_t *a = krealloc(NULL, 33), *blocker = kmalloc(64);
    assert(a && blocker); memset(a, 0x41, 33); memset(blocker, 0x22, 64);
    /* Poison source padding and future destination. Realloc may copy only the
     * requested 33 bytes, not the old rounded capacity or its block header. */
    memset(a + 33, 0xfe, 15);
    HeapBlock *destination = block_next((HeapBlock *)blocker - 1);
    memset(destination + 1, 0x63, destination->size);
    uint8_t *moved = krealloc(a, 4097);
    assert(moved && moved != a); verify_bytes(moved, 33, 0x41);
    verify_bytes(moved + 33, 4097 - 33, 0x63);
    verify_bytes(blocker, 64, 0x22);
    memset(moved, 0x33, 4097);
    assert(krealloc(moved, 17) == moved); verify_bytes(moved, 17, 0x33);
    assert(krealloc(moved, 2048) == moved); verify_bytes(moved, 17, 0x33);
    memset(moved, 0xa5, 2048);
    assert(!krealloc(moved, HEAP_MAX_BYTES) && !krealloc(moved, SIZE_MAX));
    verify_bytes(moved, 2048, 0xa5); assert(check_heap());
    assert(!krealloc(moved, 0)); kfree(blocker); clean();
    /* Exact neighbor absorption with no split. */
    a = kmalloc(64); blocker = kmalloc(64); void *c = kmalloc(64);
    assert(a && blocker && c); memset(a, 4, 64); kfree(blocker);
    assert(krealloc(a, 144) == a); verify_bytes(a, 64, 4); kfree(a); kfree(c); clean();
    /* Force real fragmentation OOM, not merely an oversized request. */
    a = kmalloc(64); blocker = kmalloc(64); assert(a && blocker); memset(a, 0x77, 64);
    HeapBlock *tail = block_next((HeapBlock *)blocker - 1);
    void *rest = kmalloc(tail->size); assert(rest && !kmalloc(1));
    uint32_t old_size = ((HeapBlock *)a - 1)->size;
    assert(!krealloc(a, 128)); verify_bytes(a, 64, 0x77);
    assert(((HeapBlock *)a - 1)->size == old_size);
    kfree(rest); kfree(blocker); kfree(a); clean();
}

static void test_exact_oom(void) {
    boot(HEAP_MAX_BYTES);
    void *ptrs[8192];
    for (unsigned cycle = 0; cycle < 3; cycle++) {
        unsigned count = 0;
        while (count < 8192 && (ptrs[count] = kmalloc(4096))) {
            memset(ptrs[count], (int)(count & 255), 4096); count++;
        }
        while (count < 8192 && (ptrs[count] = kmalloc(1))) count++;
        assert(count < 8192 && count > 4000 && !kmalloc(1));
        check_heap();
        for (unsigned i = 0; i < count; i += 2) kfree(ptrs[i]);
        check_heap();
        for (unsigned i = 1; i < count; i += 2) kfree(ptrs[i]);
        clean(); /* Recovers every byte, including all split headers. */
    }
    assert(events == 1);
}

static uint32_t rng = 0x621d3a45;
static uint32_t random_word(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
static void test_random(void) {
    struct { uint8_t *p; size_t n; uint8_t byte; } slots[128] = {0};
    boot(1024u * 1024u);
    for (unsigned turn = 0; turn < 20000; turn++) {
        unsigned i = random_word() % 128;
        if (slots[i].p) verify_bytes(slots[i].p, slots[i].n, slots[i].byte);
        if (random_word() % 3 == 0) {
            kfree(slots[i].p); slots[i].p = NULL; slots[i].n = 0;
        } else {
            size_t n = random_word() % 32769;
            uint8_t *p = krealloc(slots[i].p, n);
            if (!n) { assert(!p); slots[i].p = NULL; slots[i].n = 0; }
            else if (p) {
                if (slots[i].p) verify_bytes(p, n < slots[i].n ? n : slots[i].n, slots[i].byte);
                slots[i].p = p; slots[i].n = n; slots[i].byte = (uint8_t)random_word();
                memset(p, slots[i].byte, n);
            } else if (slots[i].p) verify_bytes(slots[i].p, slots[i].n, slots[i].byte);
        }
        check_heap();
    }
    for (unsigned i = 0; i < 128; i++) {
        if (slots[i].p) verify_bytes(slots[i].p, slots[i].n, slots[i].byte);
        kfree(slots[i].p);
    }
    clean();
}

static void test_corruption_bounds(void) {
    boot(65536);
    uint8_t *a = kmalloc(64), *b = kmalloc(64);
    assert(a && b); memset(a, 1, 64); memset(b, 2, 64);
    HeapBlock *header = (HeapBlock *)a - 1;
    header->size = UINT32_MAX; /* May not walk outside the mapped arena. */
    assert(!kmalloc(1) && !krealloc(a, 32));
    kfree(a); kfree(b);
    verify_bytes(a, 64, 1); verify_bytes(b, 64, 2);
    output_len = 0; heap_render(); assert(strstr(output, "CORRUPT metadata"));
    kfree_all(); clean();
    a = kmalloc(64); assert(a);
    ((HeapBlock *)a - 1)->guard ^= 1;
    assert(!kmalloc(1)); kfree(a);
    kfree_all(); clean();
}

int main(void) {
    test_low_memory(); test_large_and_boundaries(); test_invalid_and_fragmentation();
    test_realloc(); test_exact_oom(); test_random(); test_corruption_bounds();
    assert(munmap(mapping, HEAP_MAX_BYTES + 2 * PAGE_SIZE) == 0);
    puts("heap native: PASS (6 MiB + 2 MiB writable, lowmem, alignment, overflow, realloc, OOM, 20000 randomized operations)");
    return 0;
}

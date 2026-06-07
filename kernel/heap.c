#include "kernel.h"

typedef struct HeapBlock {
    uint32_t size;
    uint32_t used;
    struct HeapBlock *next;
} HeapBlock;

static uint8_t *heap_base;
static uint32_t heap_bytes;
static uint32_t heap_used;
static HeapBlock *head;

static uint32_t align8(uint32_t n) { return (n + 7) & ~7u; }

void heap_init(void) {
    uint32_t frame = pmm_alloc_frame();
    heap_base = (uint8_t *)frame;
    heap_bytes = frame ? PAGE_SIZE : 0;
    heap_used = 0;
    head = 0;
    wyrd_append("memory", "init", "kernel-heap", frame ? "ok" : "fail", "single-page bump/list heap online");
}

void *kmalloc(size_t size) {
    if (!size || !heap_base) return 0;
    uint32_t need = align8((uint32_t)size);
    for (HeapBlock *b = head; b; b = b->next) {
        if (!b->used && b->size >= need) {
            b->used = 1;
            wyrd_append("heap", "alloc", "block", "ok", "reused free block");
            return (void *)(b + 1);
        }
    }
    uint32_t total = sizeof(HeapBlock) + need;
    if (heap_used + total > heap_bytes) {
        wyrd_append("heap", "alloc", "block", "fail", "heap exhausted");
        return 0;
    }
    HeapBlock *b = (HeapBlock *)(heap_base + heap_used);
    b->size = need;
    b->used = 1;
    b->next = head;
    head = b;
    heap_used += total;
    wyrd_append("heap", "alloc", "block", "ok", "new heap block granted");
    return (void *)(b + 1);
}

void kfree_all(void) {
    for (HeapBlock *b = head; b; b = b->next) b->used = 0;
    wyrd_append("heap", "free", "all", "ok", "all heap blocks marked free");
}

void heap_render(void) {
    vga_set_color(COLOR_LIGHT_CYAN, COLOR_BLACK);
    vga_write("kernel heap\n");
    vga_set_color(COLOR_LIGHT_GREY, COLOR_BLACK);
    vga_write("  base:  "); vga_write_hex((uint32_t)heap_base); vga_putc('\n');
    vga_write("  size:  "); vga_write_dec(heap_bytes); vga_write(" bytes\n");
    vga_write("  used:  "); vga_write_dec(heap_used); vga_write(" bytes\n");
    uint32_t i = 0;
    for (HeapBlock *b = head; b; b = b->next) {
        vga_write("  block "); vga_write_dec(i++);
        vga_write(" size="); vga_write_dec(b->size);
        vga_write(" used="); vga_write_dec(b->used); vga_putc('\n');
    }
}

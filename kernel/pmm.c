#include "kernel.h"

extern uint32_t kernel_start;
extern uint32_t kernel_end;

static uint8_t used[PMM_MAX_FRAMES / 8];
/* Permanent eligibility is independent of allocation state. */
static uint8_t eligible[PMM_MAX_FRAMES / 8];
static uint32_t total_frames;
static uint32_t free_count;

static void bit_set(uint32_t i) { used[i / 8] |= (uint8_t)(1u << (i % 8)); }
static void bit_clear(uint32_t i) { used[i / 8] &= (uint8_t)~(1u << (i % 8)); }
static int bit_test(uint32_t i) { return used[i / 8] & (uint8_t)(1u << (i % 8)); }

static void visit_memory(uint64_t base, uint64_t len, int usable) {
    const uint64_t cap = (uint64_t)PMM_MAX_FRAMES * PAGE_SIZE;
    if (!len || base >= cap || len > UINT64_MAX - base) return;
    uint64_t top = base + len;
    if (top > cap) top = cap;
    uint32_t start = (uint32_t)((base + (usable ? PAGE_SIZE - 1 : 0)) / PAGE_SIZE);
    uint32_t end = (uint32_t)((top + (usable ? 0 : PAGE_SIZE - 1)) / PAGE_SIZE);
    if (usable && end > total_frames) total_frames = end;
    for (uint32_t i = start; i < end; i++) {
        if (usable) {
            eligible[i / 8] |= (uint8_t)(1u << (i % 8));
            if (bit_test(i)) { bit_clear(i); free_count++; }
        } else {
            eligible[i / 8] &= (uint8_t)~(1u << (i % 8));
            if (!bit_test(i)) { bit_set(i); free_count--; }
        }
    }
}

void pmm_init(void) {
    memset(used, 0xFF, sizeof(used));
    memset(eligible, 0, sizeof(eligible));
    total_frames = 0;
    free_count = 0;
    bootinfo_visit_memory(visit_memory);
    visit_memory(0, 0x100000, 0);
    uint64_t start = (uintptr_t)&kernel_start, end = (uintptr_t)&kernel_end;
    if (end < start) {
        memset(used, 0xFF, sizeof(used));
        memset(eligible, 0, sizeof(eligible));
        free_count = total_frames = 0;
    } else visit_memory(start, end - start, 0);
    wyrd_append("memory", "init", "physical-frames", free_count ? "ok" : "fail", "validated bitmap allocator initialized");
}

uint32_t pmm_total_frames(void) { return total_frames; }
uint32_t pmm_free_frames(void) { return free_count; }

uint32_t pmm_alloc_frame(void) {
    for (uint32_t i = 256; i < total_frames; i++) {
        if (!bit_test(i)) {
            bit_set(i);
            if (free_count) free_count--;
            uint32_t addr = i * PAGE_SIZE;
            wyrd_append("memory", "alloc", "frame", "ok", "4KiB physical page granted");
            return addr;
        }
    }
    wyrd_append("memory", "alloc", "frame", "fail", "out of physical frames");
    return 0;
}

void pmm_free_frame(uint32_t frame_addr) {
    uint32_t i = frame_addr / PAGE_SIZE;
    if (frame_addr % PAGE_SIZE || i >= total_frames || !(eligible[i / 8] & (1u << (i % 8)))) {
        wyrd_append("memory", "free", "frame", "deny", "invalid or reserved frame");
        return;
    }
    if (bit_test(i)) { bit_clear(i); free_count++; }
    wyrd_append("memory", "free", "frame", "ok", "4KiB physical page returned");
}

void pmm_render(void) {
    vga_set_color(COLOR_LIGHT_CYAN, COLOR_BLACK);
    vga_write("physical memory manager\n");
    vga_set_color(COLOR_LIGHT_GREY, COLOR_BLACK);
    vga_write("  page_size:    "); vga_write_dec(PAGE_SIZE); vga_write(" bytes\n");
    vga_write("  total_frames: "); vga_write_dec(total_frames); vga_putc('\n');
    vga_write("  free_frames:  "); vga_write_dec(free_count); vga_putc('\n');
    vga_write("  free_memory:  "); vga_write_dec((free_count * PAGE_SIZE) / 1024); vga_write(" KiB\n");
    vga_write("  kernel_start: "); vga_write_hex((uint32_t)(uintptr_t)&kernel_start); vga_putc('\n');
    vga_write("  kernel_end:   "); vga_write_hex((uint32_t)(uintptr_t)&kernel_end); vga_putc('\n');
}

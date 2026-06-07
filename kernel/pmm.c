#include "kernel.h"

extern uint32_t kernel_start;
extern uint32_t kernel_end;

static uint8_t used[PMM_MAX_FRAMES / 8];
static uint32_t total_frames;
static uint32_t free_count;

static void bit_set(uint32_t i) { used[i / 8] |= (uint8_t)(1u << (i % 8)); }
static void bit_clear(uint32_t i) { used[i / 8] &= (uint8_t)~(1u << (i % 8)); }
static int bit_test(uint32_t i) { return used[i / 8] & (uint8_t)(1u << (i % 8)); }

static void mark_range_free(uint32_t base, uint32_t len) {
    uint32_t start = (base + PAGE_SIZE - 1) / PAGE_SIZE;
    uint32_t end = (base + len) / PAGE_SIZE;
    if (end > PMM_MAX_FRAMES) end = PMM_MAX_FRAMES;
    for (uint32_t i = start; i < end; i++) {
        if (bit_test(i)) { bit_clear(i); free_count++; }
    }
}

static void mark_range_used(uint32_t base, uint32_t len) {
    uint32_t start = base / PAGE_SIZE;
    uint32_t end = (base + len + PAGE_SIZE - 1) / PAGE_SIZE;
    if (end > PMM_MAX_FRAMES) end = PMM_MAX_FRAMES;
    for (uint32_t i = start; i < end; i++) {
        if (!bit_test(i)) { bit_set(i); if (free_count) free_count--; }
    }
}

void pmm_init(void) {
    memset(used, 0xFF, sizeof(used));
    total_frames = (bootinfo_mem_kib() * 1024) / PAGE_SIZE;
    if (total_frames > PMM_MAX_FRAMES) total_frames = PMM_MAX_FRAMES;
    free_count = 0;
    /* Conservative QEMU baseline: free 1 MiB..detected-top, then reserve kernel and low memory. */
    if (total_frames > 256) mark_range_free(0x100000, (total_frames * PAGE_SIZE) - 0x100000);
    mark_range_used(0, 0x100000);
    mark_range_used((uint32_t)&kernel_start, (uint32_t)&kernel_end - (uint32_t)&kernel_start);
    wyrd_append("memory", "init", "physical-frames", "ok", "bitmap allocator online");
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
    if (i < 256 || i >= total_frames) {
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
    vga_write("  kernel_start: "); vga_write_hex((uint32_t)&kernel_start); vga_putc('\n');
    vga_write("  kernel_end:   "); vga_write_hex((uint32_t)&kernel_end); vga_putc('\n');
}

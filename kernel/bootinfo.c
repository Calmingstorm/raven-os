#include "kernel.h"

static uint32_t mb_magic;
static MultibootInfo *mbi;
static uint32_t total_mem_kib;
static uint32_t mmap_entries;
static uint64_t highest_usable;

void bootinfo_init(uint32_t magic, uint32_t info_addr) {
    mb_magic = magic;
    mbi = (MultibootInfo *)info_addr;
    total_mem_kib = 0;
    mmap_entries = 0;
    highest_usable = 0;

    if (magic != MULTIBOOT_BOOTLOADER_MAGIC || !mbi) {
        wyrd_append("boot", "inspect", "multiboot", "fail", "magic or info pointer invalid");
        return;
    }

    if (mbi->flags & 0x1) total_mem_kib = mbi->mem_lower + mbi->mem_upper;
    if (mbi->flags & (1 << 6)) {
        uint32_t end = mbi->mmap_addr + mbi->mmap_length;
        for (uint32_t ptr = mbi->mmap_addr; ptr < end;) {
            MultibootMmapEntry *e = (MultibootMmapEntry *)ptr;
            mmap_entries++;
            if (e->type == 1) {
                uint64_t top = e->addr + e->len;
                if (top > highest_usable) highest_usable = top;
            }
            ptr += e->size + sizeof(e->size);
        }
    }
    wyrd_append("boot", "inspect", "multiboot", "ok", "memory map captured");
}

uint32_t bootinfo_mem_kib(void) {
    return total_mem_kib;
}

void bootinfo_render(void) {
    vga_set_color(COLOR_LIGHT_CYAN, COLOR_BLACK);
    vga_write("boot information\n");
    vga_set_color(COLOR_LIGHT_GREY, COLOR_BLACK);
    vga_write("  magic: "); vga_write_hex(mb_magic); vga_putc('\n');
    if (!mbi || mb_magic != MULTIBOOT_BOOTLOADER_MAGIC) {
        vga_write("  multiboot info unavailable\n");
        return;
    }
    vga_write("  mem_lower: "); vga_write_dec(mbi->mem_lower); vga_write(" KiB\n");
    vga_write("  mem_upper: "); vga_write_dec(mbi->mem_upper); vga_write(" KiB\n");
    vga_write("  total:     "); vga_write_dec(total_mem_kib); vga_write(" KiB\n");
    vga_write("  mmap:      "); vga_write_dec(mmap_entries); vga_write(" entries\n");
    vga_write("  usable_top:"); vga_write_hex((uint32_t)highest_usable); vga_putc('\n');

    if (mbi->flags & (1 << 6)) {
        uint32_t end = mbi->mmap_addr + mbi->mmap_length;
        uint32_t shown = 0;
        for (uint32_t ptr = mbi->mmap_addr; ptr < end && shown < 8;) {
            MultibootMmapEntry *e = (MultibootMmapEntry *)ptr;
            vga_write("  region "); vga_write_dec(shown);
            vga_write(" base="); vga_write_hex((uint32_t)e->addr);
            vga_write(" len="); vga_write_hex((uint32_t)e->len);
            vga_write(" type="); vga_write_dec(e->type); vga_putc('\n');
            ptr += e->size + sizeof(e->size);
            shown++;
        }
    }
}

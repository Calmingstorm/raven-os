#include "kernel.h"

#define SECTOR_SIZE 512
#define DISK_SECTORS 128
static uint8_t disk[DISK_SECTORS][SECTOR_SIZE];
static uint32_t reads;
static uint32_t writes;

void storage_init(void) {
    memset(disk, 0, sizeof(disk));
    const char *sig = "RAVENFS0 volatile block device; ATA driver pending";
    memcpy(disk[0], sig, strlen(sig));
    reads = writes = 0;
    wyrd_append("storage", "init", "ramdisk0", "ok", "128 volatile sectors online");
}

int storage_write_sector(uint32_t lba, const uint8_t *src) {
    if (lba >= DISK_SECTORS) {
        wyrd_append("storage", "write", "sector", "fail", "lba out of range");
        return 0;
    }
    memcpy(disk[lba], src, SECTOR_SIZE);
    writes++;
    wyrd_append("storage", "write", "ramdisk0", "ok", "sector written");
    return 1;
}

int storage_read_sector(uint32_t lba, uint8_t *dst) {
    if (lba >= DISK_SECTORS) {
        wyrd_append("storage", "read", "sector", "fail", "lba out of range");
        return 0;
    }
    memcpy(dst, disk[lba], SECTOR_SIZE);
    reads++;
    wyrd_append("storage", "read", "ramdisk0", "ok", "sector read");
    return 1;
}

void storage_render(void) {
    uint8_t buf[SECTOR_SIZE];
    storage_read_sector(0, buf);
    vga_set_color(COLOR_LIGHT_CYAN, COLOR_BLACK);
    vga_write("storage\n");
    vga_set_color(COLOR_LIGHT_GREY, COLOR_BLACK);
    vga_write("  device: ramdisk0\n");
    vga_write("  sectors: "); vga_write_dec(DISK_SECTORS); vga_write(" x "); vga_write_dec(SECTOR_SIZE); vga_write(" bytes\n");
    vga_write("  reads: "); vga_write_dec(reads); vga_write(" writes: "); vga_write_dec(writes); vga_putc('\n');
    vga_write("  sector0: ");
    for (uint32_t i = 0; i < 56 && buf[i]; i++) vga_putc((char)buf[i]);
    vga_putc('\n');
}

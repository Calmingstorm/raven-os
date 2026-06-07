#include "kernel.h"

#define ATA_DATA 0x1F0
#define ATA_ERROR 0x1F1
#define ATA_SECCOUNT0 0x1F2
#define ATA_LBA0 0x1F3
#define ATA_LBA1 0x1F4
#define ATA_LBA2 0x1F5
#define ATA_HDDEVSEL 0x1F6
#define ATA_COMMAND 0x1F7
#define ATA_STATUS 0x1F7
#define ATA_CMD_READ_PIO 0x20
#define ATA_CMD_WRITE_PIO 0x30
#define ATA_CMD_CACHE_FLUSH 0xE7
#define ATA_CMD_IDENTIFY 0xEC
#define ATA_SR_BSY 0x80
#define ATA_SR_DRDY 0x40
#define ATA_SR_DRQ 0x08
#define ATA_SR_ERR 0x01

static uint8_t ata_present;
static uint8_t ata_rw_ready;
static uint16_t identify_words[256];
static char model[41];
static uint32_t ata_reads;
static uint32_t ata_writes;
static uint32_t ata_errors;

static uint16_t inw(uint16_t port) {
    uint16_t ret;
    __asm__ volatile ("inw %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static void outw(uint16_t port, uint16_t value) {
    __asm__ volatile ("outw %0, %1" : : "a"(value), "Nd"(port));
}

static void tiny_wait(void) {
    inb(ATA_STATUS); inb(ATA_STATUS); inb(ATA_STATUS); inb(ATA_STATUS);
}

static int wait_not_busy(void) {
    uint32_t spin = 1000000;
    uint8_t status = inb(ATA_STATUS);
    while ((status & ATA_SR_BSY) && spin--) status = inb(ATA_STATUS);
    if (!spin || (status & ATA_SR_ERR)) {
        ata_errors++;
        return 0;
    }
    return 1;
}

static int wait_drq(void) {
    uint32_t spin = 1000000;
    uint8_t status = inb(ATA_STATUS);
    while (!(status & ATA_SR_DRQ) && spin--) {
        if (status & ATA_SR_ERR) { ata_errors++; return 0; }
        status = inb(ATA_STATUS);
    }
    if (!spin) { ata_errors++; return 0; }
    return 1;
}

static void select_lba(uint32_t lba, uint8_t count) {
    outb(ATA_HDDEVSEL, (uint8_t)(0xE0 | ((lba >> 24) & 0x0F)));
    tiny_wait();
    outb(ATA_SECCOUNT0, count);
    outb(ATA_LBA0, (uint8_t)(lba & 0xFF));
    outb(ATA_LBA1, (uint8_t)((lba >> 8) & 0xFF));
    outb(ATA_LBA2, (uint8_t)((lba >> 16) & 0xFF));
}

void ata_init(void) {
    memset(identify_words, 0, sizeof(identify_words));
    memset(model, 0, sizeof(model));
    ata_present = 0;
    ata_rw_ready = 0;
    ata_reads = ata_writes = ata_errors = 0;

    outb(ATA_HDDEVSEL, 0xA0);
    tiny_wait();
    outb(ATA_SECCOUNT0, 0);
    outb(ATA_LBA0, 0);
    outb(ATA_LBA1, 0);
    outb(ATA_LBA2, 0);
    outb(ATA_COMMAND, ATA_CMD_IDENTIFY);
    tiny_wait();
    uint8_t status = inb(ATA_STATUS);
    if (status == 0) {
        wyrd_append("ata", "probe", "primary-master", "miss", "no status from port 0x1F7");
        return;
    }
    if (!wait_not_busy()) {
        wyrd_append("ata", "identify", "primary-master", "fail", "busy/error during identify");
        return;
    }
    if (!wait_drq()) {
        wyrd_append("ata", "identify", "primary-master", "miss", "device did not request data");
        return;
    }
    for (uint32_t i = 0; i < 256; i++) identify_words[i] = inw(ATA_DATA);
    for (uint32_t i = 0; i < 20; i++) {
        uint16_t w = identify_words[27 + i];
        model[i * 2] = (char)(w >> 8);
        model[i * 2 + 1] = (char)(w & 0xFF);
    }
    model[40] = '\0';
    ata_present = 1;
    ata_rw_ready = 1;
    wyrd_append("ata", "identify", "primary-master", "ok", model);
}

uint32_t ata_present_flag(void) { return ata_present; }
uint32_t ata_rw_ready_flag(void) { return ata_rw_ready; }

int ata_read_sector(uint32_t lba, uint8_t *dst) {
    if (!ata_rw_ready || !dst) {
        wyrd_append("ata", "read", "sector", "fail", "ata not ready");
        return 0;
    }
    if (!wait_not_busy()) return 0;
    select_lba(lba, 1);
    outb(ATA_COMMAND, ATA_CMD_READ_PIO);
    if (!wait_not_busy() || !wait_drq()) {
        wyrd_append("ata", "read", "sector", "fail", "device not ready for data");
        return 0;
    }
    uint16_t *words = (uint16_t *)dst;
    for (uint32_t i = 0; i < 256; i++) words[i] = inw(ATA_DATA);
    ata_reads++;
    wyrd_append("ata", "read", "sector", "ok", "pio sector read");
    return 1;
}

int ata_write_sector(uint32_t lba, const uint8_t *src) {
    if (!ata_rw_ready || !src) {
        wyrd_append("ata", "write", "sector", "fail", "ata not ready");
        return 0;
    }
    if (!wait_not_busy()) return 0;
    select_lba(lba, 1);
    outb(ATA_COMMAND, ATA_CMD_WRITE_PIO);
    if (!wait_not_busy() || !wait_drq()) {
        wyrd_append("ata", "write", "sector", "fail", "device not ready for data");
        return 0;
    }
    const uint16_t *words = (const uint16_t *)src;
    for (uint32_t i = 0; i < 256; i++) outw(ATA_DATA, words[i]);
    outb(ATA_COMMAND, ATA_CMD_CACHE_FLUSH);
    wait_not_busy();
    ata_writes++;
    wyrd_append("ata", "write", "sector", "ok", "pio sector written");
    return 1;
}

void ata_render(void) {
    vga_set_color(COLOR_LIGHT_CYAN, COLOR_BLACK);
    vga_write("ata probe\n");
    vga_set_color(COLOR_LIGHT_GREY, COLOR_BLACK);
    vga_write("  primary master: "); vga_write(ata_present ? "present" : "not detected"); vga_putc('\n');
    vga_write("  pio rw ready: "); vga_write(ata_rw_ready ? "yes" : "no"); vga_putc('\n');
    if (ata_present) { vga_write("  model: "); vga_write(model); vga_putc('\n'); }
    vga_write("  reads/writes/errors: "); vga_write_dec(ata_reads); vga_write("/"); vga_write_dec(ata_writes); vga_write("/"); vga_write_dec(ata_errors); vga_putc('\n');
    vga_write("  commands: ata-read and ata-write exercise LBA 20 in dev images\n");
}

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
#define ATA_CMD_IDENTIFY 0xEC
#define ATA_SR_BSY 0x80
#define ATA_SR_DRDY 0x40
#define ATA_SR_DRQ 0x08
#define ATA_SR_ERR 0x01

static uint8_t ata_present;
static uint16_t identify_words[256];
static char model[41];

static uint16_t inw(uint16_t port) {
    uint16_t ret;
    __asm__ volatile ("inw %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static void tiny_wait(void) {
    inb(ATA_STATUS); inb(ATA_STATUS); inb(ATA_STATUS); inb(ATA_STATUS);
}

void ata_init(void) {
    memset(identify_words, 0, sizeof(identify_words));
    memset(model, 0, sizeof(model));
    ata_present = 0;

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
    uint32_t spin = 100000;
    while ((status & ATA_SR_BSY) && spin--) status = inb(ATA_STATUS);
    if (status & ATA_SR_ERR) {
        wyrd_append("ata", "identify", "primary-master", "fail", "error bit set");
        return;
    }
    if (!(status & ATA_SR_DRQ)) {
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
    wyrd_append("ata", "identify", "primary-master", "ok", model);
}

uint32_t ata_present_flag(void) { return ata_present; }

void ata_render(void) {
    vga_set_color(COLOR_LIGHT_CYAN, COLOR_BLACK);
    vga_write("ata probe\n");
    vga_set_color(COLOR_LIGHT_GREY, COLOR_BLACK);
    vga_write("  primary master: "); vga_write(ata_present ? "present" : "not detected"); vga_putc('\n');
    if (ata_present) { vga_write("  model: "); vga_write(model); vga_putc('\n'); }
    vga_write("  mode: identify-only scaffold; read/write driver pending\n");
}

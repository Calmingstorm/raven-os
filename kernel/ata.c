#include "kernel.h"
#include "raven_ata.h"

#define ATA_DATA 0x1F0
#define ATA_SECCOUNT0 0x1F2
#define ATA_LBA0 0x1F3
#define ATA_LBA1 0x1F4
#define ATA_LBA2 0x1F5
#define ATA_HDDEVSEL 0x1F6
#define ATA_COMMAND 0x1F7
#define ATA_STATUS 0x1F7
#define ATA_ALTSTATUS 0x3F6
#define ATA_CMD_READ_PIO 0x20
#define ATA_CMD_WRITE_PIO 0x30
#define ATA_CMD_CACHE_FLUSH 0xE7
#define ATA_CMD_IDENTIFY 0xEC
#define ATA_SR_BSY 0x80
#define ATA_SR_DF 0x20
#define ATA_SR_DRQ 0x08
#define ATA_SR_ERR 0x01
#define ATA_LBA28_SECTORS 0x10000000u
#ifndef RAVEN_ATA_POLL_LIMIT
#define RAVEN_ATA_POLL_LIMIT 1000000u
#endif

typedef struct {
    uint8_t attempted, present;
    uint32_t sectors;
    int error;
    char model[41];
} AtaDrive;

static AtaDrive drives[2];
static uint32_t ata_reads, ata_writes, ata_errors;

static int fail(unsigned drive, int error) {
    drives[drive].error = error;
    if (drive == RAVEN_ATA_MASTER) ata_errors++;
    return 0;
}

static void tiny_wait(void) {
    /* 400 ns after selection/command or the final data word. */
    inb(ATA_ALTSTATUS); inb(ATA_ALTSTATUS);
    inb(ATA_ALTSTATUS); inb(ATA_ALTSTATUS);
}

enum { WAIT_PRECOMMAND, WAIT_DRQ, WAIT_COMPLETE, WAIT_IDENTIFY };

static int wait_status(unsigned drive, int phase) {
    for (uint32_t spin = 0; spin < RAVEN_ATA_POLL_LIMIT; spin++) {
        uint8_t status = inb(ATA_STATUS);
        if (status == 0 || status == 0xff)
            return fail(drive, RAVEN_ATA_ERROR_ABSENT);
        /* DRQ and ERR are not meaningful while BSY is asserted. */
        if (status & ATA_SR_BSY) continue;
        /* Stale ERR/DF from the last command must not prevent recovery. */
        if (phase == WAIT_PRECOMMAND) return 1;
        if (status & (ATA_SR_ERR | ATA_SR_DF))
            return fail(drive, RAVEN_ATA_ERROR_IO);
        if (phase == WAIT_IDENTIFY) return 1;
        if (phase == WAIT_DRQ && (status & ATA_SR_DRQ)) return 1;
        if (phase == WAIT_COMPLETE && !(status & ATA_SR_DRQ)) return 1;
    }
    return fail(drive, RAVEN_ATA_ERROR_TIMEOUT);
}

static int select_drive(unsigned drive, uint8_t head) {
    outb(ATA_HDDEVSEL, (uint8_t)(head | (drive << 4)));
    tiny_wait();
    return wait_status(drive, WAIT_PRECOMMAND);
}

int raven_ata_probe(unsigned drive) {
    if (drive > RAVEN_ATA_SLAVE) return 0;
    AtaDrive *dev = &drives[drive];
    memset(dev, 0, sizeof(*dev));
    dev->attempted = 1;
    if (!select_drive(drive, 0xA0)) return 0;
    outb(ATA_SECCOUNT0, 0);
    outb(ATA_LBA0, 0);
    outb(ATA_LBA1, 0);
    outb(ATA_LBA2, 0);
    outb(ATA_COMMAND, ATA_CMD_IDENTIFY);
    tiny_wait();
    if (!wait_status(drive, WAIT_IDENTIFY)) return 0;
    /* IDENTIFY PACKET/ATAPI signatures must never be treated as ATA disks. */
    if (inb(ATA_LBA1) != 0 || inb(ATA_LBA2) != 0)
        return fail(drive, RAVEN_ATA_ERROR_IO);
    if (!wait_status(drive, WAIT_DRQ)) return 0;
    uint16_t words[256];
    for (uint32_t i = 0; i < 256; i++) words[i] = inw(ATA_DATA);
    tiny_wait();
    if (!wait_status(drive, WAIT_COMPLETE)) return 0;
    if ((words[0] & 0x8000u) || !(words[49] & (1u << 9)))
        return fail(drive, RAVEN_ATA_ERROR_IO);
    uint32_t sectors = (uint32_t)words[60] | ((uint32_t)words[61] << 16);
    if (!sectors) return fail(drive, RAVEN_ATA_ERROR_IO);
    dev->sectors = sectors < ATA_LBA28_SECTORS ? sectors : ATA_LBA28_SECTORS;
    for (uint32_t i = 0; i < 20; i++) {
        dev->model[i * 2] = (char)(words[27 + i] >> 8);
        dev->model[i * 2 + 1] = (char)words[27 + i];
    }
    dev->model[40] = '\0';
    dev->present = 1;
    dev->error = RAVEN_ATA_ERROR_NONE;
    return 1;
}

static int ensure_drive(unsigned drive) {
    if (!drives[drive].attempted) return raven_ata_probe(drive);
    return drives[drive].present;
}

uint32_t raven_ata_sector_count(unsigned drive) {
    if (drive > RAVEN_ATA_SLAVE || !ensure_drive(drive)) return 0;
    return drives[drive].sectors;
}

int raven_ata_error(unsigned drive) {
    return drive <= RAVEN_ATA_SLAVE ? drives[drive].error : RAVEN_ATA_ERROR_INVALID;
}

static int prepare_io(unsigned drive, uint32_t lba, const void *buffer) {
    if (!buffer) return fail(drive, RAVEN_ATA_ERROR_INVALID);
    if (lba >= ATA_LBA28_SECTORS) return fail(drive, RAVEN_ATA_ERROR_RANGE);
    if (!ensure_drive(drive)) return 0;
    if (lba >= drives[drive].sectors) return fail(drive, RAVEN_ATA_ERROR_RANGE);
    if (!select_drive(drive, (uint8_t)(0xE0 | ((lba >> 24) & 0x0f)))) return 0;
    outb(ATA_SECCOUNT0, 1);
    outb(ATA_LBA0, (uint8_t)lba);
    outb(ATA_LBA1, (uint8_t)(lba >> 8));
    outb(ATA_LBA2, (uint8_t)(lba >> 16));
    return 1;
}

int raven_ata_read_sector(unsigned drive, uint32_t lba, uint8_t *dst) {
    if (drive > RAVEN_ATA_SLAVE) return 0;
    if (!prepare_io(drive, lba, dst)) return 0;
    outb(ATA_COMMAND, ATA_CMD_READ_PIO);
    tiny_wait();
    if (!wait_status(drive, WAIT_DRQ)) return 0;
    for (uint32_t i = 0; i < 256; i++) {
        uint16_t word = inw(ATA_DATA);
        dst[i * 2] = (uint8_t)word;
        dst[i * 2 + 1] = (uint8_t)(word >> 8);
    }
    tiny_wait();
    if (!wait_status(drive, WAIT_COMPLETE)) return 0;
    drives[drive].error = RAVEN_ATA_ERROR_NONE;
    if (drive == RAVEN_ATA_MASTER) ata_reads++;
    return 1;
}

void ata_init(void) {
    memset(drives, 0, sizeof(drives));
    ata_reads = ata_writes = ata_errors = 0;
    if (!raven_ata_probe(RAVEN_ATA_MASTER)) {
        if (raven_ata_error(RAVEN_ATA_MASTER) == RAVEN_ATA_ERROR_ABSENT)
            wyrd_append("ata", "probe", "primary-master", "miss", "no status from port 0x1F7");
        else
            wyrd_append("ata", "identify", "primary-master", "fail", "busy/error during identify");
        return;
    }
    wyrd_append("ata", "identify", "primary-master", "ok", drives[0].model);
}

uint32_t ata_present_flag(void) { return drives[0].present; }
uint32_t ata_rw_ready_flag(void) { return drives[0].present; }

int ata_read_sector(uint32_t lba, uint8_t *dst) {
    int ok = raven_ata_read_sector(RAVEN_ATA_MASTER, lba, dst);
    wyrd_append("ata", "read", "sector", ok ? "ok" : "fail",
                ok ? "pio sector read" : "device not ready for data");
    return ok;
}

int ata_write_sector(uint32_t lba, const uint8_t *src) {
    unsigned drive = RAVEN_ATA_MASTER;
    int ok = prepare_io(drive, lba, src);
    if (ok) {
        outb(ATA_COMMAND, ATA_CMD_WRITE_PIO);
        tiny_wait();
        ok = wait_status(drive, WAIT_DRQ);
    }
    if (ok) {
        for (uint32_t i = 0; i < 256; i++)
            outw(ATA_DATA, (uint16_t)((uint16_t)src[i * 2] | ((uint16_t)src[i * 2 + 1] << 8)));
        tiny_wait();
        ok = wait_status(drive, WAIT_COMPLETE);
    }
    if (ok) {
        outb(ATA_COMMAND, ATA_CMD_CACHE_FLUSH);
        tiny_wait();
        ok = wait_status(drive, WAIT_COMPLETE);
    }
    if (ok) {
        drives[drive].error = RAVEN_ATA_ERROR_NONE;
        ata_writes++;
    }
    wyrd_append("ata", "write", "sector", ok ? "ok" : "fail",
                ok ? "pio sector written" : "device not ready for data");
    return ok;
}

void ata_render(void) {
    vga_set_color(COLOR_LIGHT_CYAN, COLOR_BLACK);
    vga_write("ata probe\n");
    vga_set_color(COLOR_LIGHT_GREY, COLOR_BLACK);
    vga_write("  primary master: "); vga_write(drives[0].present ? "present" : "not detected"); vga_putc('\n');
    vga_write("  pio rw ready: "); vga_write(drives[0].present ? "yes" : "no"); vga_putc('\n');
    if (drives[0].present) { vga_write("  model: "); vga_write(drives[0].model); vga_putc('\n'); }
    vga_write("  reads/writes/errors: "); vga_write_dec(ata_reads); vga_write("/"); vga_write_dec(ata_writes); vga_write("/"); vga_write_dec(ata_errors); vga_putc('\n');
    vga_write("  commands: ata-read and ata-write exercise LBA 20 in dev images\n");
}

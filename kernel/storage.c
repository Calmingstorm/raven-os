#include "kernel.h"

typedef struct {
    uint8_t used;
    char path[48];
    char content[320];
} DiskFile;

#define DISK_MAGIC "RAVENFS1"
#define DISK_FILE_CAP 24
#define RAVENFS_ATA_BASE_LBA 32

static uint8_t disk[DISK_SECTORS][SECTOR_SIZE];
static uint32_t reads;
static uint32_t writes;
static uint8_t ata_backed;
static DiskFile mounted[DISK_FILE_CAP];
static char load_buf[320];

static void write_magic(void) {
    memset(disk[0], 0, SECTOR_SIZE);
    memcpy(disk[0], DISK_MAGIC, strlen(DISK_MAGIC));
}

static int magic_valid(void) {
    return !strncmp((const char *)disk[0], DISK_MAGIC, strlen(DISK_MAGIC));
}

static int backend_write(uint32_t lba, const uint8_t *src) {
    if (ata_backed) return ata_write_sector(RAVENFS_ATA_BASE_LBA + lba, src);
    return 1;
}

static int backend_read(uint32_t lba, uint8_t *dst) {
    if (ata_backed) return ata_read_sector(RAVENFS_ATA_BASE_LBA + lba, dst);
    return 0;
}

static uint32_t file_sector(uint32_t index) {
    return 1 + index;
}

static void encode_file(uint32_t index) {
    if (index >= DISK_FILE_CAP) return;
    uint8_t *sec = disk[file_sector(index)];
    memset(sec, 0, SECTOR_SIZE);
    sec[0] = mounted[index].used;
    memcpy(sec + 1, mounted[index].path, sizeof(mounted[index].path));
    memcpy(sec + 1 + sizeof(mounted[index].path), mounted[index].content, sizeof(mounted[index].content));
}

static void decode_file(uint32_t index) {
    if (index >= DISK_FILE_CAP) return;
    uint8_t *sec = disk[file_sector(index)];
    mounted[index].used = sec[0] ? 1 : 0;
    memcpy(mounted[index].path, sec + 1, sizeof(mounted[index].path));
    mounted[index].path[sizeof(mounted[index].path) - 1] = '\0';
    memcpy(mounted[index].content, sec + 1 + sizeof(mounted[index].path), sizeof(mounted[index].content));
    mounted[index].content[sizeof(mounted[index].content) - 1] = '\0';
}

static void sync_all(void) {
    write_magic();
    backend_write(0, disk[0]);
    for (uint32_t i = 0; i < DISK_FILE_CAP; i++) {
        encode_file(i);
        backend_write(file_sector(i), disk[file_sector(i)]);
    }
}

static void mount_from_backend(void) {
    if (!ata_backed) return;
    if (!backend_read(0, disk[0]) || !magic_valid()) {
        storage_format();
        storage_save_file("/persist/readme", "RavenFS ATA-backed image. Files survive QEMU reboot.");
        wyrd_append("storage", "mount", "ata-ravenfs", "new", "formatted empty persistent filesystem");
        return;
    }
    for (uint32_t i = 0; i < DISK_FILE_CAP; i++) {
        if (backend_read(file_sector(i), disk[file_sector(i)])) decode_file(i);
    }
    wyrd_append("storage", "mount", "ata-ravenfs", "ok", "persistent file table loaded from disk");
}

void storage_init(void) {
    memset(disk, 0, sizeof(disk));
    memset(mounted, 0, sizeof(mounted));
    reads = writes = 0;
    ata_backed = ata_rw_ready_flag() ? 1 : 0;
    if (ata_backed) {
        mount_from_backend();
        wyrd_append("storage", "init", "ravenfs", "ok", "ATA-backed block fs online");
    } else {
        storage_format();
        storage_save_file("/persist/readme", "RavenFS volatile image. Attach ATA disk for persistence.");
        wyrd_append("storage", "init", "ravenfs", "ok", "formatted volatile block fs");
    }
}

int storage_write_sector(uint32_t lba, const uint8_t *src) {
    if (lba >= DISK_SECTORS) {
        wyrd_append("storage", "write", "sector", "fail", "lba out of range");
        return 0;
    }
    memcpy(disk[lba], src, SECTOR_SIZE);
    if (!backend_write(lba, src)) {
        wyrd_append("storage", "write", "ravenfs", "fail", "backend rejected sector");
        return 0;
    }
    writes++;
    if (lba > 0 && lba <= DISK_FILE_CAP) decode_file(lba - 1);
    wyrd_append("storage", "write", "ravenfs", "ok", "sector written");
    return 1;
}

int storage_read_sector(uint32_t lba, uint8_t *dst) {
    if (lba >= DISK_SECTORS) {
        wyrd_append("storage", "read", "sector", "fail", "lba out of range");
        return 0;
    }
    if (ata_backed && backend_read(lba, disk[lba])) {
        if (lba > 0 && lba <= DISK_FILE_CAP) decode_file(lba - 1);
    }
    memcpy(dst, disk[lba], SECTOR_SIZE);
    reads++;
    wyrd_append("storage", "read", "ravenfs", "ok", "sector read");
    return 1;
}

uint32_t storage_reads(void) { return reads; }
uint32_t storage_writes(void) { return writes; }

int storage_format(void) {
    memset(disk, 0, sizeof(disk));
    memset(mounted, 0, sizeof(mounted));
    sync_all();
    writes += 1 + DISK_FILE_CAP;
    wyrd_append("storage", "format", "ravenfs", "ok", "superblock and file slots reset");
    return 1;
}

int storage_save_file(const char *path, const char *content) {
    if (!path || !content || !path[0]) return 0;
    uint32_t slot = DISK_FILE_CAP;
    for (uint32_t i = 0; i < DISK_FILE_CAP; i++) {
        if (mounted[i].used && !strcmp(mounted[i].path, path)) { slot = i; break; }
    }
    if (slot == DISK_FILE_CAP) {
        for (uint32_t i = 0; i < DISK_FILE_CAP; i++) {
            if (!mounted[i].used) { slot = i; break; }
        }
    }
    if (slot == DISK_FILE_CAP) {
        wyrd_append("storage", "save", path, "fail", "ravenfs file table full");
        return 0;
    }
    mounted[slot].used = 1;
    strcopy(mounted[slot].path, path, sizeof(mounted[slot].path));
    strcopy(mounted[slot].content, content, sizeof(mounted[slot].content));
    encode_file(slot);
    if (!backend_write(file_sector(slot), disk[file_sector(slot)])) return 0;
    writes++;
    wyrd_append("storage", "save", path, "ok", "file persisted to block image");
    return 1;
}

const char *storage_load_file(const char *path) {
    if (!path) return 0;
    for (uint32_t i = 0; i < DISK_FILE_CAP; i++) {
        if (mounted[i].used && !strcmp(mounted[i].path, path)) {
            strcopy(load_buf, mounted[i].content, sizeof(load_buf));
            reads++;
            wyrd_append("storage", "load", path, "ok", "file loaded from block image");
            return load_buf;
        }
    }
    wyrd_append("storage", "load", path, "miss", "file not found");
    return 0;
}

int storage_has_file(const char *path) {
    if (!path) return 0;
    for (uint32_t i = 0; i < DISK_FILE_CAP; i++) {
        if (mounted[i].used && !strcmp(mounted[i].path, path)) return 1;
    }
    return 0;
}

int storage_delete_file(const char *path) {
    if (!path) return 0;
    for (uint32_t i = 0; i < DISK_FILE_CAP; i++) {
        if (mounted[i].used && !strcmp(mounted[i].path, path)) {
            memset(&mounted[i], 0, sizeof(mounted[i]));
            encode_file(i);
            if (!backend_write(file_sector(i), disk[file_sector(i)])) return 0;
            writes++;
            wyrd_append("storage", "delete", path, "ok", "file removed from ravenfs");
            return 1;
        }
    }
    wyrd_append("storage", "delete", path, "miss", "file not found");
    return 0;
}

void storage_ls_prefix(const char *prefix) {
    const char *pfx = prefix && prefix[0] ? prefix : "/";
    vga_write("ravenfs list "); vga_write(pfx); vga_putc('\n');
    for (uint32_t i = 0; i < DISK_FILE_CAP; i++) {
        if (mounted[i].used && strprefix(mounted[i].path, pfx)) {
            vga_write("  "); vga_write(mounted[i].path); vga_write("  "); vga_write_dec(strlen(mounted[i].content)); vga_write(" bytes\n");
        }
    }
}

void storage_ls(void) {
    vga_write("ravenfs files\n");
    for (uint32_t i = 0; i < DISK_FILE_CAP; i++) {
        if (mounted[i].used) {
            vga_write("  "); vga_write(mounted[i].path); vga_write(" = "); vga_write(mounted[i].content); vga_putc('\n');
        }
    }
}

void storage_render(void) {
    uint8_t buf[SECTOR_SIZE];
    storage_read_sector(0, buf);
    vga_set_color(COLOR_LIGHT_CYAN, COLOR_BLACK);
    vga_write("storage\n");
    vga_set_color(COLOR_LIGHT_GREY, COLOR_BLACK);
    vga_write("  device: ravenfs0 "); vga_write(ata_backed ? "ATA-backed persistent image" : "volatile block image"); vga_putc('\n');
    if (ata_backed) { vga_write("  ata-base-lba: "); vga_write_dec(RAVENFS_ATA_BASE_LBA); vga_putc('\n'); }
    vga_write("  sectors: "); vga_write_dec(DISK_SECTORS); vga_write(" x "); vga_write_dec(SECTOR_SIZE); vga_write(" bytes\n");
    vga_write("  reads: "); vga_write_dec(reads); vga_write(" writes: "); vga_write_dec(writes); vga_putc('\n');
    vga_write("  superblock: ");
    for (uint32_t i = 0; i < 16 && buf[i]; i++) vga_putc((char)buf[i]);
    vga_putc('\n');
    storage_ls();
}

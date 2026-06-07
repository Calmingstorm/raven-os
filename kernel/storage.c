#include "kernel.h"

typedef struct {
    uint8_t used;
    char path[48];
    char content[320];
    uint32_t checksum;
} DiskFile;

#define DISK_MAGIC "RAVENFS1"
#define DISK_FILE_CAP 24
#define RAVENFS_ATA_BASE_LBA 32
#define JOURNAL_MAGIC "RAVENJ1"
#define JOURNAL_SECTOR (1 + DISK_FILE_CAP)
#define JOURNAL_OP_SAVE 1
#define JOURNAL_OP_DELETE 2

static uint8_t disk[DISK_SECTORS][SECTOR_SIZE];
static uint32_t reads;
static uint32_t writes;
static uint8_t ata_backed;
static DiskFile mounted[DISK_FILE_CAP];
static char load_buf[320];
static uint32_t journal_seq;

static int backend_write(uint32_t lba, const uint8_t *src);
static int backend_read(uint32_t lba, uint8_t *dst);
static uint32_t file_sector(uint32_t index);
static void encode_file(uint32_t index);

static uint32_t checksum_bytes(const uint8_t *data, uint32_t len) {
    uint32_t h = 2166136261u;
    for (uint32_t i = 0; i < len; i++) {
        h ^= data[i];
        h *= 16777619u;
    }
    return h;
}

static uint32_t checksum_file_fields(const char *path, const char *content) {
    uint32_t h = checksum_bytes((const uint8_t *)path, (uint32_t)strlen(path));
    uint32_t c = checksum_bytes((const uint8_t *)content, (uint32_t)strlen(content));
    return h ^ (c + 0x9e3779b9u + (h << 6) + (h >> 2));
}

static void write_u32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xff);
    p[1] = (uint8_t)((v >> 8) & 0xff);
    p[2] = (uint8_t)((v >> 16) & 0xff);
    p[3] = (uint8_t)((v >> 24) & 0xff);
}

static uint32_t read_u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}


typedef struct {
    uint8_t active;
    uint32_t seq;
    uint32_t op;
    uint32_t slot;
    DiskFile file;
    uint32_t checksum;
} JournalEntry;

static uint32_t checksum_mix_u32(uint32_t h, uint32_t v) {
    uint8_t b[4];
    write_u32(b, v);
    uint32_t c = checksum_bytes(b, sizeof(b));
    return h ^ (c + 0x9e3779b9u + (h << 6) + (h >> 2));
}

static uint32_t checksum_journal_fields(const JournalEntry *j) {
    uint32_t h = checksum_mix_u32(2166136261u, j->active);
    h = checksum_mix_u32(h, j->seq);
    h = checksum_mix_u32(h, j->op);
    h = checksum_mix_u32(h, j->slot);
    h ^= checksum_bytes((const uint8_t *)j->file.path, (uint32_t)strlen(j->file.path));
    h ^= checksum_bytes((const uint8_t *)j->file.content, (uint32_t)strlen(j->file.content));
    h = checksum_mix_u32(h, j->file.used);
    h = checksum_mix_u32(h, j->file.checksum);
    return h;
}

static void encode_journal(const JournalEntry *j) {
    uint8_t *sec = disk[JOURNAL_SECTOR];
    memset(sec, 0, SECTOR_SIZE);
    memcpy(sec, JOURNAL_MAGIC, strlen(JOURNAL_MAGIC));
    sec[8] = j->active;
    write_u32(sec + 9, j->seq);
    write_u32(sec + 13, j->op);
    write_u32(sec + 17, j->slot);
    sec[21] = j->file.used;
    memcpy(sec + 22, j->file.path, sizeof(j->file.path));
    memcpy(sec + 70, j->file.content, sizeof(j->file.content));
    write_u32(sec + 390, j->file.checksum);
    write_u32(sec + 394, checksum_journal_fields(j));
}

static int decode_journal(JournalEntry *j) {
    uint8_t *sec = disk[JOURNAL_SECTOR];
    if (strncmp((const char *)sec, JOURNAL_MAGIC, strlen(JOURNAL_MAGIC))) return 0;
    memset(j, 0, sizeof(*j));
    j->active = sec[8] ? 1 : 0;
    j->seq = read_u32(sec + 9);
    j->op = read_u32(sec + 13);
    j->slot = read_u32(sec + 17);
    j->file.used = sec[21] ? 1 : 0;
    memcpy(j->file.path, sec + 22, sizeof(j->file.path));
    j->file.path[sizeof(j->file.path) - 1] = '\0';
    memcpy(j->file.content, sec + 70, sizeof(j->file.content));
    j->file.content[sizeof(j->file.content) - 1] = '\0';
    j->file.checksum = read_u32(sec + 390);
    j->checksum = read_u32(sec + 394);
    return j->checksum == checksum_journal_fields(j);
}

static int journal_write(uint32_t op, uint32_t slot, const DiskFile *file) {
    JournalEntry j;
    memset(&j, 0, sizeof(j));
    j.active = 1;
    j.seq = ++journal_seq;
    j.op = op;
    j.slot = slot;
    if (file) memcpy(&j.file, file, sizeof(j.file));
    encode_journal(&j);
    if (!backend_write(JOURNAL_SECTOR, disk[JOURNAL_SECTOR])) {
        wyrd_append("storage", "journal", "write", "fail", "backend rejected journal sector");
        return 0;
    }
    writes++;
    wyrd_append("storage", "journal", "write", "ok", "transaction intent persisted");
    return 1;
}

static int journal_clear(void) {
    memset(disk[JOURNAL_SECTOR], 0, SECTOR_SIZE);
    if (!backend_write(JOURNAL_SECTOR, disk[JOURNAL_SECTOR])) {
        wyrd_append("storage", "journal", "clear", "fail", "backend rejected journal clear");
        return 0;
    }
    writes++;
    wyrd_append("storage", "journal", "clear", "ok", "transaction completed");
    return 1;
}

static int apply_slot_from_journal(const JournalEntry *j) {
    if (!j || !j->active || j->slot >= DISK_FILE_CAP) return 0;
    if (j->op == JOURNAL_OP_SAVE) {
        memcpy(&mounted[j->slot], &j->file, sizeof(mounted[j->slot]));
    } else if (j->op == JOURNAL_OP_DELETE) {
        memset(&mounted[j->slot], 0, sizeof(mounted[j->slot]));
    } else {
        return 0;
    }
    encode_file(j->slot);
    if (!backend_write(file_sector(j->slot), disk[file_sector(j->slot)])) return 0;
    writes++;
    return 1;
}

static void recover_journal(void) {
    if (!backend_read(JOURNAL_SECTOR, disk[JOURNAL_SECTOR])) return;
    JournalEntry j;
    if (!decode_journal(&j)) return;
    if (!j.active) return;
    if (apply_slot_from_journal(&j)) {
        journal_seq = j.seq;
        journal_clear();
        wyrd_append("storage", "journal", "recover", "ok", "replayed interrupted ravenfs transaction");
    } else {
        wyrd_append("storage", "journal", "recover", "fail", "could not replay interrupted transaction");
    }
}

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
    if (mounted[index].used) mounted[index].checksum = checksum_file_fields(mounted[index].path, mounted[index].content);
    memcpy(sec + 1, mounted[index].path, sizeof(mounted[index].path));
    memcpy(sec + 1 + sizeof(mounted[index].path), mounted[index].content, sizeof(mounted[index].content));
    write_u32(sec + 1 + sizeof(mounted[index].path) + sizeof(mounted[index].content), mounted[index].checksum);
}

static void decode_file(uint32_t index) {
    if (index >= DISK_FILE_CAP) return;
    uint8_t *sec = disk[file_sector(index)];
    mounted[index].used = sec[0] ? 1 : 0;
    memcpy(mounted[index].path, sec + 1, sizeof(mounted[index].path));
    mounted[index].path[sizeof(mounted[index].path) - 1] = '\0';
    memcpy(mounted[index].content, sec + 1 + sizeof(mounted[index].path), sizeof(mounted[index].content));
    mounted[index].content[sizeof(mounted[index].content) - 1] = '\0';
    mounted[index].checksum = read_u32(sec + 1 + sizeof(mounted[index].path) + sizeof(mounted[index].content));
}

static void sync_all(void) {
    write_magic();
    backend_write(0, disk[0]);
    for (uint32_t i = 0; i < DISK_FILE_CAP; i++) {
        encode_file(i);
        backend_write(file_sector(i), disk[file_sector(i)]);
    }
    memset(disk[JOURNAL_SECTOR], 0, SECTOR_SIZE);
    backend_write(JOURNAL_SECTOR, disk[JOURNAL_SECTOR]);
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
    recover_journal();
    wyrd_append("storage", "mount", "ata-ravenfs", "ok", "persistent file table loaded from disk");
}

void storage_init(void) {
    memset(disk, 0, sizeof(disk));
    memset(mounted, 0, sizeof(mounted));
    reads = writes = 0;
    journal_seq = 0;
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
    DiskFile next;
    memset(&next, 0, sizeof(next));
    next.used = 1;
    strcopy(next.path, path, sizeof(next.path));
    strcopy(next.content, content, sizeof(next.content));
    next.checksum = checksum_file_fields(next.path, next.content);
    if (!journal_write(JOURNAL_OP_SAVE, slot, &next)) return 0;
    if (!apply_slot_from_journal(&(JournalEntry){ .active = 1, .op = JOURNAL_OP_SAVE, .slot = slot, .file = next })) return 0;
    if (!journal_clear()) return 0;
    wyrd_append("storage", "save", path, "ok", "file persisted to block image");
    return 1;
}

int storage_append_file(const char *path, const char *suffix) {
    if (!path || !suffix || !path[0]) return 0;
    for (uint32_t i = 0; i < DISK_FILE_CAP; i++) {
        if (mounted[i].used && !strcmp(mounted[i].path, path)) {
            size_t cur = strlen(mounted[i].content);
            size_t add = strlen(suffix);
            if (cur + add + 1 >= sizeof(mounted[i].content)) {
                wyrd_append("storage", "append", path, "fail", "ravenfs file would exceed fixed buffer");
                return 0;
            }
            DiskFile next;
            memcpy(&next, &mounted[i], sizeof(next));
            memcpy(next.content + cur, suffix, add + 1);
            next.checksum = checksum_file_fields(next.path, next.content);
            if (!journal_write(JOURNAL_OP_SAVE, i, &next)) return 0;
            if (!apply_slot_from_journal(&(JournalEntry){ .active = 1, .op = JOURNAL_OP_SAVE, .slot = i, .file = next })) return 0;
            if (!journal_clear()) return 0;
            wyrd_append("storage", "append", path, "ok", "file extended on block image");
            return 1;
        }
    }
    return storage_save_file(path, suffix);
}

int storage_sync_ramfs(void) {
    uint32_t synced = 0;
    uint32_t count = ramfs_count();
    for (uint32_t i = 0; i < count; i++) {
        const char *path = ramfs_path_at(i);
        const char *content = path ? ramfs_read(path) : 0;
        if (path && content && storage_save_file(path, content)) synced++;
    }
    vga_write("synced ramfs files: "); vga_write_dec(synced); vga_putc('\n');
    wyrd_append("storage", "sync", "ramfs", "ok", "ramfs flushed into ravenfs");
    return (int)synced;
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
            if (!journal_write(JOURNAL_OP_DELETE, i, 0)) return 0;
            if (!apply_slot_from_journal(&(JournalEntry){ .active = 1, .op = JOURNAL_OP_DELETE, .slot = i })) return 0;
            if (!journal_clear()) return 0;
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

int storage_fsck(void) {
    uint32_t used = 0;
    uint32_t errors = 0;
    if (!magic_valid()) errors++;
    for (uint32_t i = 0; i < DISK_FILE_CAP; i++) {
        if (!mounted[i].used) continue;
        used++;
        uint32_t expected = checksum_file_fields(mounted[i].path, mounted[i].content);
        if (mounted[i].checksum != expected) errors++;
        if (!mounted[i].path[0] || mounted[i].path[0] != '/') errors++;
    }
    vga_write("ravenfs fsck: files="); vga_write_dec(used);
    vga_write(" errors="); vga_write_dec(errors);
    vga_write(errors ? " status=fail" : " status=clean");
    JournalEntry j;
    vga_write(" journal=");
    vga_write(decode_journal(&j) && j.active ? "pending" : "clean");
    vga_putc('\n');
    wyrd_append("storage", "fsck", "ravenfs", errors ? "fail" : "ok", errors ? "integrity errors found" : "file table checksums clean");
    return errors ? 0 : 1;
}

void storage_debug_corrupt(const char *path) {
    if (!path) return;
    for (uint32_t i = 0; i < DISK_FILE_CAP; i++) {
        if (mounted[i].used && !strcmp(mounted[i].path, path)) {
            mounted[i].checksum ^= 0xfeedfaceu;
            encode_file(i);
            disk[file_sector(i)][1 + sizeof(mounted[i].path) + sizeof(mounted[i].content)] ^= 0x55;
            decode_file(i);
            backend_write(file_sector(i), disk[file_sector(i)]);
            wyrd_append("storage", "debug-corrupt", path, "ok", "checksum deliberately damaged");
            return;
        }
    }
}

int storage_debug_stage_journal_save(const char *path, const char *content) {
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
    if (slot == DISK_FILE_CAP) return 0;
    DiskFile next;
    memset(&next, 0, sizeof(next));
    next.used = 1;
    strcopy(next.path, path, sizeof(next.path));
    strcopy(next.content, content, sizeof(next.content));
    next.checksum = checksum_file_fields(next.path, next.content);
    return journal_write(JOURNAL_OP_SAVE, slot, &next);
}

void storage_ls(void) {
    vga_write("ravenfs files\n");
    for (uint32_t i = 0; i < DISK_FILE_CAP; i++) {
        if (mounted[i].used) {
            vga_write("  "); vga_write(mounted[i].path); vga_write(" = "); vga_write(mounted[i].content);
            vga_write(" checksum="); vga_write_hex(mounted[i].checksum); vga_putc('\n');
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
    vga_write("  journal-sector: "); vga_write_dec(JOURNAL_SECTOR); vga_putc('\n');
    vga_write("  sectors: "); vga_write_dec(DISK_SECTORS); vga_write(" x "); vga_write_dec(SECTOR_SIZE); vga_write(" bytes\n");
    vga_write("  reads: "); vga_write_dec(reads); vga_write(" writes: "); vga_write_dec(writes); vga_putc('\n');
    uint32_t used = 0; uint32_t bytes = 0;
    for (uint32_t i = 0; i < DISK_FILE_CAP; i++) { if (mounted[i].used) { used++; bytes += strlen(mounted[i].path) + strlen(mounted[i].content); } }
    vga_write("  files: "); vga_write_dec(used); vga_write("/"); vga_write_dec(DISK_FILE_CAP); vga_write(" approx-bytes: "); vga_write_dec(bytes); vga_putc('\n');
    vga_write("  superblock: ");
    for (uint32_t i = 0; i < 16 && buf[i]; i++) vga_putc((char)buf[i]);
    vga_putc('\n');
    storage_ls();
}

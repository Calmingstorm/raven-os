#include <assert.h>
#include "../libc/include/errno.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "kernel.h"
#include "raven_ata.h"
#include "raven_wad.h"

int errno;
static uint8_t image[RAVEN_WAD_IMAGE_BYTES];
static uint8_t master[160][512];
static uint8_t master_before[sizeof(master)];
static unsigned probes, slave_reads, master_reads, master_writes, log_calls;
static uint32_t capacity = RAVEN_WAD_IMAGE_BYTES / 512u, master_capacity;
static int probe_ok = 1, last_error, read_fault_error;
static uint32_t read_fault_lba = UINT32_MAX;

int raven_ata_probe(unsigned drive) {
    assert(drive == RAVEN_ATA_SLAVE);
    ++probes;
    return probe_ok;
}
uint32_t raven_ata_sector_count(unsigned drive) {
    assert(drive == RAVEN_ATA_MASTER || drive == RAVEN_ATA_SLAVE);
    return drive == RAVEN_ATA_MASTER ? master_capacity : capacity;
}
int raven_ata_error(unsigned drive) {
    assert(drive == RAVEN_ATA_SLAVE);
    return last_error;
}
int raven_ata_read_sector(unsigned drive, uint32_t lba, uint8_t *dst) {
    assert(drive == RAVEN_ATA_SLAVE);
    assert(lba < capacity);
    assert(lba < RAVEN_WAD_IMAGE_BYTES / 512u);
    ++slave_reads;
    if (lba == read_fault_lba) {
        /* Device failures may dirty the destination before reporting error. */
        memset(dst, 0xcc, 512);
        last_error = read_fault_error;
        return 0;
    }
    memcpy(dst, image + lba * 512u, 512);
    return 1;
}
uint32_t ata_rw_ready_flag(void) { return 1; }
int ata_read_sector(uint32_t lba, uint8_t *dst) {
    assert(lba < master_capacity && lba < 160);
    ++master_reads;
    memcpy(dst, master[lba], 512);
    return 1;
}
int ata_write_sector(uint32_t lba, const uint8_t *src) {
    assert(lba < master_capacity && lba < 160);
    ++master_writes;
    memcpy(master[lba], src, 512);
    return 1;
}
void wyrd_append(const char *a, const char *b, const char *c,
                 const char *d, const char *e) {
    (void)a; (void)b; (void)c; (void)d; (void)e;
    ++log_calls;
}
void strcopy(char *dst, const char *src, size_t cap) {
    if (!cap) return;
    size_t i = 0;
    for (; i + 1 < cap && src[i]; ++i) dst[i] = src[i];
    dst[i] = 0;
}
int strprefix(const char *s, const char *prefix) {
    return !strncmp(s, prefix, strlen(prefix));
}
uint32_t ramfs_count(void) { return 0; }
const char *ramfs_path_at(uint32_t i) { (void)i; return NULL; }
const char *ramfs_read(const char *path) { (void)path; return NULL; }
void vga_write(const char *s) { (void)s; }
void vga_write_dec(uint32_t i) { (void)i; }
void vga_write_hex(uint32_t i) { (void)i; }
void vga_putc(char c) { (void)c; }
void vga_set_color(uint8_t a, uint8_t b) { (void)a; (void)b; }

static void put32(uint32_t offset, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) image[offset + i] = value >> (i * 8u);
}
static void fixture(void) {
    raven_wad_unmount();
    for (uint32_t i = 0; i < sizeof(image); ++i) image[i] = (uint8_t)(i * 31u + 7u);
    memcpy(image, "IWAD", 4);
    put32(4, 3);
    put32(8, 1017); /* Directory entry crosses a sector boundary. */
    put32(1017, 12); put32(1021, 100);
    put32(1033, RAVEN_WAD_LOGICAL_BYTES); put32(1037, 0);
    put32(1049, RAVEN_WAD_LOGICAL_BYTES - 100); put32(1053, 100);
    memset(image + RAVEN_WAD_LOGICAL_BYTES, 0xee,
           RAVEN_WAD_IMAGE_BYTES - RAVEN_WAD_LOGICAL_BYTES);
    probes = slave_reads = 0;
    capacity = RAVEN_WAD_IMAGE_BYTES / 512u;
    read_fault_lba = UINT32_MAX;
    last_error = RAVEN_ATA_ERROR_NONE;
    probe_ok = 1;
}
static void bad_mount(void) {
    assert(raven_wad_mount() == -1);
    assert(errno == EINVAL);
}
static void bounds_tests(void) {
    fixture(); capacity = 28795392u / 512u; bad_mount(); /* Old sector-only image is too short. */
    fixture(); capacity = RAVEN_WAD_IMAGE_BYTES / 512u - 1u; bad_mount();
    fixture(); image[0] = 'P'; bad_mount();
    fixture(); put32(4, 0); bad_mount();
    fixture(); put32(4, UINT32_MAX); bad_mount();
    fixture(); put32(8, 11); bad_mount();
    fixture(); put32(8, UINT32_MAX); bad_mount();
    fixture(); put32(8, RAVEN_WAD_LOGICAL_BYTES - 15); bad_mount();
    fixture(); put32(1017, RAVEN_WAD_LOGICAL_BYTES + 1); bad_mount();
    fixture(); put32(1021, UINT32_MAX); bad_mount();
    fixture(); put32(1017, RAVEN_WAD_LOGICAL_BYTES); put32(1021, 1); bad_mount();
    fixture(); put32(1033, UINT32_MAX); bad_mount(); /* Zero marker still bounded. */
    fixture(); --capacity; bad_mount(); assert(!slave_reads);
    fixture(); /* Exact end-of-file directory is valid. */
    put32(4, 1); put32(8, RAVEN_WAD_LOGICAL_BYTES - 16);
    put32(RAVEN_WAD_LOGICAL_BYTES - 16, 12);
    put32(RAVEN_WAD_LOGICAL_BYTES - 12, 1);
    assert(raven_wad_mount() == 0);
}
static void io_tests(void) {
    fixture(); probe_ok = 0; last_error = RAVEN_ATA_ERROR_ABSENT;
    assert(raven_wad_mount() == -1 && errno == ENOENT && !slave_reads);
    fixture(); probe_ok = 0; last_error = RAVEN_ATA_ERROR_TIMEOUT;
    assert(raven_wad_mount() == -1 && errno == ETIMEDOUT);
    fixture(); read_fault_lba = 0; read_fault_error = RAVEN_ATA_ERROR_IO;
    assert(raven_wad_mount() == -1 && errno == EIO);
    read_fault_lba = UINT32_MAX;
    assert(raven_wad_mount() == 0); /* Failed mount never poisons cache/retry. */
    fixture(); read_fault_lba = 2; read_fault_error = RAVEN_ATA_ERROR_TIMEOUT;
    assert(raven_wad_mount() == -1 && errno == ETIMEDOUT);
    fixture();
    RavenWadFile *file = raven_wad_open(RAVEN_WAD_PATH);
    assert(file);
    uint8_t buffer[32]; size_t count = 77;
    read_fault_lba = 8; read_fault_error = RAVEN_ATA_ERROR_IO;
    memset(buffer, 0x5a, sizeof(buffer));
    assert(raven_wad_read_at(file, 4095, buffer, 10, &count) == -1);
    assert(errno == EIO && count == 1 && buffer[0] == image[4095]);
    for (unsigned i = 1; i < sizeof(buffer); ++i) assert(buffer[i] == 0x5a);
    read_fault_lba = UINT32_MAX;
    assert(raven_wad_read_at(file, 4096, buffer, 10, &count) == 0);
    assert(count == 10 && !memcmp(buffer, image + 4096, 10));
}
static void read_handle_cache_tests(void) {
    fixture();
    unsigned logs = log_calls, reads = master_reads, writes = master_writes;
    assert(raven_wad_open(NULL) == NULL && errno == EINVAL && !probes);
    assert(raven_wad_open("/wad/../freedoom1.wad") == NULL && errno == ENOENT && !probes);
    RavenWadFile *file = raven_wad_open(RAVEN_WAD_BASENAME);
    assert(file && probes == 1 && slave_reads <= 4);
    unsigned before = slave_reads;
    assert(raven_wad_mount() == 0 && probes == 1 && slave_reads == before);
    uint32_t size = 77;
    assert(raven_wad_size(file, &size) == 0 && size == RAVEN_WAD_LOGICAL_BYTES);
    uint8_t buffer[1024]; size_t count;
    assert(raven_wad_read_at(file, 4093, buffer + 1, 700, &count) == 0);
    assert(count == 700 && !memcmp(buffer + 1, image + 4093, count));
    before = slave_reads;
    for (unsigned i = 0; i < 20; ++i) {
        assert(raven_wad_read_at(file, 4093, buffer + 1, 700, &count) == 0);
        assert(count == 700);
    }
    assert(slave_reads == before);
    /* More than eight distinct sectors forces bounded cache replacement. */
    for (unsigned i = 0; i < 20; ++i)
        assert(raven_wad_read_at(file, i * 512u, buffer, 1, &count) == 0);
    assert(raven_wad_read_at(file, 4093, buffer + 1, 700, &count) == 0);
    assert(!memcmp(buffer + 1, image + 4093, count));
    memset(buffer, 0x5a, sizeof(buffer));
    assert(raven_wad_read_at(file, RAVEN_WAD_LOGICAL_BYTES - 517,
                              buffer + 1, SIZE_MAX, &count) == 0);
    assert(count == 517 && buffer[0] == 0x5a && buffer[518] == 0x5a);
    assert(!memcmp(buffer + 1, image + RAVEN_WAD_LOGICAL_BYTES - 517, count));
    assert(raven_wad_read_at(file, RAVEN_WAD_LOGICAL_BYTES, buffer, 10, &count) == 0 && count == 0);
    assert(raven_wad_read_at(file, UINT32_MAX, buffer, SIZE_MAX, &count) == 0 && count == 0);
    assert(raven_wad_read_at(file, 0, NULL, 0, &count) == 0 && count == 0);
    assert(raven_wad_read_at(file, 0, NULL, 1, &count) == -1 && errno == EINVAL && count == 0);
    assert(raven_wad_read_at(file, 0, buffer, 1, NULL) == -1 && errno == EINVAL);
    assert(raven_wad_size(file, NULL) == -1 && errno == EINVAL);
    RavenWadFile *handles[RAVEN_WAD_MAX_OPEN];
    handles[0] = file;
    for (unsigned i = 1; i < RAVEN_WAD_MAX_OPEN; ++i) {
        handles[i] = raven_wad_open(RAVEN_WAD_PATH);
        assert(handles[i] && handles[i] != file);
    }
    assert(raven_wad_open(RAVEN_WAD_PATH) == NULL && errno == EMFILE);
    RavenWadFile *bad[] = { NULL, (RavenWadFile *)(uintptr_t)1,
                           (RavenWadFile *)((uintptr_t)file + 1) };
    for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        size = 77; count = 77;
        assert(raven_wad_size(bad[i], &size) == -1 && errno == EBADF && size == 77);
        assert(raven_wad_read_at(bad[i], 0, buffer, 1, &count) == -1 && errno == EBADF && count == 0);
        assert(raven_wad_close(bad[i]) == -1 && errno == EBADF);
    }
    assert(raven_wad_close(handles[3]) == 0);
    assert(raven_wad_close(handles[3]) == -1 && errno == EBADF);
    size = 77; count = 77;
    assert(raven_wad_size(handles[3], &size) == -1 && errno == EBADF && size == 77);
    assert(raven_wad_read_at(handles[3], 0, buffer, 1, &count) == -1 && errno == EBADF && count == 0);
    /* Independent positional handles do not move any shared file cursor. */
    assert(raven_wad_read_at(handles[1], 6000, buffer, 20, &count) == 0 && count == 20);
    assert(!memcmp(buffer, image + 6000, 20));
    assert(raven_wad_read_at(handles[2], 12000, buffer, 20, &count) == 0 && count == 20);
    assert(!memcmp(buffer, image + 12000, 20));
    assert(raven_wad_read_at(handles[1], 6000, buffer, 20, &count) == 0 && count == 20);
    assert(!memcmp(buffer, image + 6000, 20));
    assert(raven_wad_open(RAVEN_WAD_PATH));
    raven_wad_unmount();
    assert(raven_wad_close(file) == -1 && errno == EBADF);
    assert(raven_wad_size(file, &size) == -1 && errno == EBADF);
    file = raven_wad_open(RAVEN_WAD_PATH);
    assert(file && probes == 2);
    raven_wad_unmount();
    image[0] = 'P';
    assert(raven_wad_mount() == -1 && errno == EINVAL); /* Cache was dropped. */
    assert(log_calls == logs && master_reads == reads && master_writes == writes);
}
static void scratch_storage_tests(void) {
    uint32_t sizes[] = {0, 1, 32, 64, 159};
    for (unsigned i = 0; i < sizeof(sizes) / sizeof(sizes[0]); ++i) {
        fixture();
        master_capacity = sizes[i];
        memset(master, 0xa5, sizeof(master));
        master_reads = master_writes = 0;
        storage_init();
        assert(!master_reads && !master_writes && !probes && !slave_reads);
        assert(storage_save_file("/persist/probe", "scratch survives in RAM"));
        assert(!strcmp(storage_load_file("/persist/probe"), "scratch survives in RAM"));
        assert(!master_reads && !master_writes);
        for (unsigned j = 0; j < sizeof(master); ++j)
            assert(((uint8_t *)master)[j] == 0xa5);
    }
    master_capacity = 160;
    storage_init();
    assert(master_reads && master_writes);
    assert(storage_save_file("/persist/probe", "master persistence"));
    memcpy(master_before, master, sizeof(master));
    fixture(); read_handle_cache_tests();
    assert(!memcmp(master_before, master, sizeof(master)));
    fixture(); bounds_tests(); io_tests();
    assert(!memcmp(master_before, master, sizeof(master)));
    storage_init();
    assert(!strcmp(storage_load_file("/persist/probe"), "master persistence"));
}
int main(void) {
    bounds_tests();
    io_tests();
    read_handle_cache_tests();
    scratch_storage_tests();
    puts("WAD native: bounds, errors, partial reads, cache, handles, lazy/no logs, master preservation, scratch capacity passed");
    return 0;
}

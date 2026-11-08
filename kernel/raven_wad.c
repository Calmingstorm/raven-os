#include "raven_wad.h"
#include "raven_ata.h"
#include "../libc/include/errno.h"

#define WAD_SECTOR_BYTES 512u
#define WAD_CACHE_SECTORS 8u

struct RavenWadFile { unsigned used; };
typedef struct {
    uint32_t lba;
    unsigned valid;
    uint8_t bytes[WAD_SECTOR_BYTES];
} WadCacheEntry;

static struct RavenWadFile files[RAVEN_WAD_MAX_OPEN];
static WadCacheEntry cache[WAD_CACHE_SECTORS];
static unsigned mounted;

static int fail(int error) { errno = error; return -1; }

static int disk_error(void) {
    switch (raven_ata_error(RAVEN_ATA_SLAVE)) {
    case RAVEN_ATA_ERROR_ABSENT: return ENOENT;
    case RAVEN_ATA_ERROR_TIMEOUT: return ETIMEDOUT;
    default: return EIO;
    }
}

static void drop_cache(void) {
    for (unsigned i = 0; i < WAD_CACHE_SECTORS; ++i) cache[i].valid = 0;
}

static uint32_t little32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static const uint8_t *sector(uint32_t lba) {
    WadCacheEntry *entry = &cache[lba % WAD_CACHE_SECTORS];
    if (entry->valid && entry->lba == lba) return entry->bytes;
    /* A failed/partial PIO must not leave a previously valid cache entry. */
    entry->valid = 0;
    if (!raven_ata_read_sector(RAVEN_ATA_SLAVE, lba, entry->bytes)) {
        errno = disk_error();
        return NULL;
    }
    entry->lba = lba;
    entry->valid = 1;
    return entry->bytes;
}

/* Internal ranges have already been checked against LOGICAL_BYTES. */
static int read_bytes(uint32_t offset, uint8_t *dst, size_t count, size_t *done) {
    while (count) {
        const uint8_t *data = sector(offset / WAD_SECTOR_BYTES);
        if (!data) return -1;
        unsigned within = offset % WAD_SECTOR_BYTES;
        size_t chunk = WAD_SECTOR_BYTES - within;
        if (chunk > count) chunk = count;
        for (size_t i = 0; i < chunk; ++i) dst[i] = data[within + i];
        dst += chunk;
        offset += (uint32_t)chunk;
        count -= chunk;
        *done += chunk;
    }
    return 0;
}

int raven_wad_mount(void) {
    if (mounted) return 0;
    drop_cache();
    if (!raven_ata_probe(RAVEN_ATA_SLAVE)) return fail(disk_error());
    if (raven_ata_sector_count(RAVEN_ATA_SLAVE) <
        RAVEN_WAD_IMAGE_BYTES / WAD_SECTOR_BYTES) return fail(EINVAL);

    uint8_t header[12];
    size_t done = 0;
    if (read_bytes(0, header, sizeof(header), &done)) goto io_failure;
    if (header[0] != 'I' || header[1] != 'W' ||
        header[2] != 'A' || header[3] != 'D') goto invalid;
    uint32_t lumps = little32(header + 4);
    uint32_t directory = little32(header + 8);
    /* Division/subtraction avoids overflowing on hostile 32-bit fields.
     * Directory entries are 16 bytes, including the eight-byte lump name. */
    if (!lumps || directory < sizeof(header) ||
        directory > RAVEN_WAD_LOGICAL_BYTES ||
        lumps > (RAVEN_WAD_LOGICAL_BYTES - directory) / 16u) goto invalid;
    for (uint32_t i = 0; i < lumps; ++i) {
        uint8_t entry[16];
        done = 0;
        if (read_bytes(directory + i * 16u, entry, sizeof(entry), &done))
            goto io_failure;
        uint32_t position = little32(entry);
        uint32_t length = little32(entry + 4);
        /* Zero-size markers are legal, including at logical EOF. */
        if (position > RAVEN_WAD_LOGICAL_BYTES ||
            length > RAVEN_WAD_LOGICAL_BYTES - position) goto invalid;
    }
    mounted = 1;
    return 0;
invalid:
    drop_cache();
    return fail(EINVAL);
io_failure:
    drop_cache();
    return -1;
}

static int path_equal(const char *a, const char *b) {
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}

/* Compare identities before dereferencing: foreign, NULL and interior pointers
 * are EBADF, rather than an arbitrary memory access in kernel mode. As with
 * FILE*, callers must not reuse a closed handle after its slot is reopened. */
static struct RavenWadFile *valid_file(const RavenWadFile *file) {
    if (!mounted) return NULL;
    for (unsigned i = 0; i < RAVEN_WAD_MAX_OPEN; ++i)
        if (file == &files[i] && files[i].used) return &files[i];
    return NULL;
}

RavenWadFile *raven_wad_open(const char *path) {
    if (!path) { errno = EINVAL; return NULL; }
    if (!path_equal(path, RAVEN_WAD_PATH) &&
        !path_equal(path, RAVEN_WAD_BASENAME)) { errno = ENOENT; return NULL; }
    if (raven_wad_mount()) return NULL;
    for (unsigned i = 0; i < RAVEN_WAD_MAX_OPEN; ++i) {
        if (!files[i].used) {
            files[i].used = 1;
            return &files[i];
        }
    }
    errno = EMFILE;
    return NULL;
}

int raven_wad_size(const RavenWadFile *file, uint32_t *size_out) {
    if (!valid_file(file)) return fail(EBADF);
    if (!size_out) return fail(EINVAL);
    *size_out = RAVEN_WAD_LOGICAL_BYTES;
    return 0;
}

int raven_wad_read_at(RavenWadFile *file, uint32_t offset,
                      void *dst, size_t count, size_t *read_out) {
    if (read_out) *read_out = 0;
    if (!valid_file(file)) return fail(EBADF);
    if (!read_out || (count && !dst)) return fail(EINVAL);
    if (offset >= RAVEN_WAD_LOGICAL_BYTES || !count) return 0;
    uint32_t available = RAVEN_WAD_LOGICAL_BYTES - offset;
    if (count > available) count = available;
    return read_bytes(offset, dst, count, read_out);
}

int raven_wad_close(RavenWadFile *file) {
    struct RavenWadFile *slot = valid_file(file);
    if (!slot) return fail(EBADF);
    slot->used = 0;
    return 0;
}

void raven_wad_unmount(void) {
    mounted = 0;
    for (unsigned i = 0; i < RAVEN_WAD_MAX_OPEN; ++i) files[i].used = 0;
    drop_cache();
}

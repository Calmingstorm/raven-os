#ifndef RAVEN_ATA_H
#define RAVEN_ATA_H
#include <stdint.h>
enum { RAVEN_ATA_MASTER = 0, RAVEN_ATA_SLAVE = 1 };
enum {
    RAVEN_ATA_ERROR_NONE = 0,
    RAVEN_ATA_ERROR_ABSENT,
    RAVEN_ATA_ERROR_IO,
    RAVEN_ATA_ERROR_TIMEOUT,
    RAVEN_ATA_ERROR_RANGE,
    RAVEN_ATA_ERROR_INVALID
};
/* Primary IDE channel, main-loop only, PIO LBA28. No slave write API exists.
 * Probe/read return 1 on success, 0 on failure. Probe explicitly retries;
 * count/read lazily identify an unprobed drive, caching absence until reprobe.
 * Capacity is capped at the LBA28 address limit, 0 on failed identification.
 * Read accepts unaligned destinations of at least 512 bytes. A failed read
 * may have modified that buffer. These entry points never append Wyrd events.
 * Error is cleared by a successful probe/read; count does not clear it.
 * Invalid drive errors return INVALID. */
int raven_ata_probe(unsigned drive);
uint32_t raven_ata_sector_count(unsigned drive);
int raven_ata_read_sector(unsigned drive, uint32_t lba, uint8_t *dst);
int raven_ata_error(unsigned drive);
#endif

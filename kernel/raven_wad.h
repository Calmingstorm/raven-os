#ifndef RAVEN_WAD_H
#define RAVEN_WAD_H

#include <stddef.h>
#include <stdint.h>

#define RAVEN_WAD_PATH "/wad/freedoom1.wad"
#define RAVEN_WAD_BASENAME "freedoom1.wad"
#define RAVEN_WAD_LOGICAL_BYTES UINT32_C(28795076)
#define RAVEN_WAD_IMAGE_BYTES UINT32_C(28835840)
#define RAVEN_WAD_MAX_OPEN 8u

typedef struct RavenWadFile RavenWadFile;

/* Main-loop only. Lazy, read-only primary slave; never probes the master.
 * The deployment pins the image hash. Mount checks the IWAD's structure and
 * extents, not its full contents, so HTTP-backed storage remains lazy.
 * Failures return -1 (open returns NULL) and set errno. */
int raven_wad_mount(void);
RavenWadFile *raven_wad_open(const char *path);
int raven_wad_size(const RavenWadFile *file, uint32_t *size_out);
/* Positional bytes, clipped to the exact logical size, never image padding.
 * read_out is zeroed first; on I/O failure it reports the transferred prefix.
 * NULL dst is permitted only with count == 0. No shared cursor. */
int raven_wad_read_at(RavenWadFile *file, uint32_t offset,
                      void *dst, size_t count, size_t *read_out);
int raven_wad_close(RavenWadFile *file);
/* Invalidates all handles/cache, without resetting the controller or writes. */
void raven_wad_unmount(void);

#endif

#include <stdio.h>
#include <stdint.h>
#include <limits.h>
#include <errno.h>
#include <string.h>
#include "raven_wad.h"

/* Diagnostics always use serial, including while the framebuffer is owned. */
extern void serial_putc(char c);
struct RavenDoomStream {
    RavenWadFile *wad;
    long position;
    unsigned active, console, eof, error;
};
static FILE out_stream = { .active = 1, .console = 1 };
static FILE err_stream = { .active = 1, .console = 1 };
FILE *stdout = &out_stream;
FILE *stderr = &err_stream;
static FILE files[RAVEN_WAD_MAX_OPEN];

static FILE *checked(FILE *stream) {
    if (stream == &out_stream || stream == &err_stream) return stream;
    for (size_t i = 0; i < RAVEN_WAD_MAX_OPEN; ++i)
        if (stream == &files[i] && files[i].active) return stream;
    errno = EBADF;
    return NULL;
}
static int fail(FILE *s, int error) {
    if (s) s->error = 1;
    errno = error;
    return -1;
}
FILE *fopen(const char *path, const char *mode) {
    if (!path || !mode) { errno = EINVAL; return NULL; }
    if (strcmp(mode, "r") && strcmp(mode, "rb")) { errno = EROFS; return NULL; }
    if (strcmp(path, RAVEN_WAD_PATH) && strcmp(path, RAVEN_WAD_BASENAME)) {
        errno = ENOENT; return NULL;
    }
    FILE *s = NULL;
    for (size_t i = 0; i < RAVEN_WAD_MAX_OPEN; ++i)
        if (!files[i].active) { s = &files[i]; break; }
    if (!s) { errno = EMFILE; return NULL; }
    RavenWadFile *wad = raven_wad_open(path);
    if (!wad) return NULL;
    *s = (FILE){ .wad = wad, .active = 1 };
    return s;
}
int fclose(FILE *stream) {
    FILE *s = checked(stream);
    if (!s) return EOF;
    if (s->console) return fail(s, EBADF);
    int result = raven_wad_close(s->wad);
    *s = (FILE){0};
    return result ? EOF : 0;
}
size_t fread(void *dst, size_t size, size_t count, FILE *stream) {
    FILE *s = checked(stream);
    if (!s) return 0;
    if (s->console) { fail(s, EBADF); return 0; }
    if (!size || !count) return 0;
    if (count > SIZE_MAX / size) { fail(s, EOVERFLOW); return 0; }
    if (!dst) { fail(s, EINVAL); return 0; }
    uint32_t logical;
    if (raven_wad_size(s->wad, &logical)) { s->error = 1; return 0; }
    if ((uint64_t)s->position >= logical) { s->eof = 1; return 0; }
    size_t wanted = size * count;
    size_t available = logical - (uint32_t)s->position;
    size_t request = wanted < available ? wanted : available;
    size_t got = 0;
    int result = raven_wad_read_at(s->wad, (uint32_t)s->position, dst, request, &got);
    /* Provider contract guarantees got <= request, including on I/O failure. */
    s->position += (long)got;
    if (result) s->error = 1;
    else if (got < wanted) s->eof = 1;
    return got / size;
}
size_t fwrite(const void *src, size_t size, size_t count, FILE *stream) {
    FILE *s = checked(stream);
    if (!s) return 0;
    if (!size || !count) return 0;
    if (count > SIZE_MAX / size) { fail(s, EOVERFLOW); return 0; }
    if (!s->console) { fail(s, EROFS); return 0; }
    if (!src) { fail(s, EINVAL); return 0; }
    const char *bytes = src;
    size_t n = size * count;
    for (size_t i = 0; i < n; ++i) serial_putc(bytes[i]);
    return count;
}
int fseek(FILE *stream, long offset, int origin) {
    FILE *s = checked(stream);
    if (!s) return -1;
    if (s->console) return fail(s, EBADF);
    long base;
    if (origin == SEEK_SET) base = 0;
    else if (origin == SEEK_CUR) base = s->position;
    else if (origin == SEEK_END) {
        uint32_t logical;
        if (raven_wad_size(s->wad, &logical)) { s->error = 1; return -1; }
#if __SIZEOF_LONG__ < 8
        if (logical > (uint32_t)LONG_MAX) return fail(s, EOVERFLOW);
#endif
        base = (long)logical;
    } else return fail(s, EINVAL);
    if (offset > 0 && base > LONG_MAX - offset) return fail(s, EOVERFLOW);
    /* base >= 0, so -base cannot overflow (including LONG_MIN offset). */
    if (offset < -base) return fail(s, EINVAL);
    s->position = base + offset;
    s->eof = 0;
    return 0;
}
long ftell(FILE *stream) {
    FILE *s = checked(stream);
    if (!s) return -1;
    if (s->console) return fail(s, EBADF);
    return s->position;
}
int feof(FILE *stream) { FILE *s = checked(stream); return s ? (int)s->eof : 0; }
int ferror(FILE *stream) { FILE *s = checked(stream); return s ? (int)s->error : 0; }
void clearerr(FILE *stream) {
    FILE *s = checked(stream);
    if (s) { s->error = 0; s->eof = 0; }
}
int fflush(FILE *stream) {
    if (!stream) return 0; /* All output is synchronous, no buffered bytes. */
    return checked(stream) ? 0 : EOF;
}
int putchar(int c) {
    unsigned char byte = (unsigned char)c;
    return fwrite(&byte, 1, 1, stdout) == 1 ? byte : EOF;
}
int puts(const char *text) {
    if (!text) { errno = EINVAL; return EOF; }
    size_t n = strlen(text);
    if (fwrite(text, 1, n, stdout) != n || putchar('\n') == EOF) return EOF;
    return 0;
}
void raven_libc_reset_streams(void) {
    for (size_t i = 0; i < RAVEN_WAD_MAX_OPEN; ++i) {
        if (files[i].active) (void)raven_wad_close(files[i].wad);
        files[i] = (FILE){0};
    }
    out_stream.error = out_stream.eof = err_stream.error = err_stream.eof = 0;
}

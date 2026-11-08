#ifndef RAVEN_STDIO_H
#define RAVEN_STDIO_H
#include <stddef.h>
#include <stdarg.h>
typedef struct RavenDoomStream FILE;
extern FILE *stdout;
extern FILE *stderr;
#define EOF (-1)
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
FILE *fopen(const char *path, const char *mode);
int fclose(FILE *stream);
size_t fread(void *dst, size_t size, size_t count, FILE *stream);
size_t fwrite(const void *src, size_t size, size_t count, FILE *stream);
int fseek(FILE *stream, long offset, int origin);
long ftell(FILE *stream);
int feof(FILE *stream);
int ferror(FILE *stream);
void clearerr(FILE *stream);
int fflush(FILE *stream);
int putchar(int c);
int puts(const char *text);
int printf(const char *format, ...);
int fprintf(FILE *stream, const char *format, ...);
int vprintf(const char *format, va_list args);
int vfprintf(FILE *stream, const char *format, va_list args);
int snprintf(char *dst, size_t capacity, const char *format, ...);
int vsnprintf(char *dst, size_t capacity, const char *format, va_list args);
int sscanf(const char *src, const char *format, ...);
int remove(const char *path);
int rename(const char *old_path, const char *new_path);
/* Runtime cleanup, before raven_wad_unmount(). No allocations. */
void raven_libc_reset_streams(void);
#endif

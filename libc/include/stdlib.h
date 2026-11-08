#ifndef RAVEN_STDLIB_H
#define RAVEN_STDLIB_H
#include <stddef.h>
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
void *malloc(size_t n);
void *calloc(size_t n, size_t size);
void *realloc(void *ptr, size_t n);
void free(void *ptr);
int abs(int value);
int atoi(const char *s);
char *getenv(const char *name);
_Noreturn void exit(int status);
_Noreturn void abort(void);
/* No atof: retained config-float paths must be removed. See libc/README.md. */
#endif

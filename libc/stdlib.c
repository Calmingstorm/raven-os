#include <stdlib.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include "raven_doom_runtime.h"

void *malloc(size_t n) { return raven_doom_malloc(n); }
void *calloc(size_t n, size_t size) { return raven_doom_calloc(n, size); }
void *realloc(void *ptr, size_t n) { return raven_doom_realloc(ptr, n); }
void free(void *ptr) { raven_doom_free(ptr); }
int abs(int value) { return value < 0 ? -value : value; }
/* Defined extension: clamp out-of-range input and set ERANGE, rather than UB. */
int atoi(const char *s) {
    while (isspace((unsigned char)*s)) ++s;
    int negative = *s == '-';
    if (*s == '-' || *s == '+') ++s;
    unsigned limit = negative ? (unsigned)INT_MAX + 1U : (unsigned)INT_MAX;
    unsigned value = 0;
    int overflow = 0;
    while (isdigit((unsigned char)*s)) {
        unsigned d = (unsigned)(*s++ - '0');
        if (value > (limit - d) / 10U) overflow = 1;
        else if (!overflow) value = value * 10U + d;
    }
    if (overflow) { errno = ERANGE; return negative ? INT_MIN : INT_MAX; }
    if (negative && value == (unsigned)INT_MAX + 1U) return INT_MIN;
    return negative ? -(int)value : (int)value;
}
char *getenv(const char *name) { (void)name; return NULL; }
_Noreturn void exit(int status) { raven_doom_exit(status); }
_Noreturn void abort(void) { raven_doom_abort("abort()"); }

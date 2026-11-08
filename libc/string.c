#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdint.h>

/* memcpy/memset/strlen/strcmp/strncmp are provided by kernel/string.c. */
void *memmove(void *dst, const void *src, size_t n) {
    unsigned char *d = dst;
    const unsigned char *s = src;
    if ((uintptr_t)d < (uintptr_t)s) {
        for (size_t i = 0; i < n; ++i) d[i] = s[i];
    } else if (d != s) {
        while (n) { --n; d[n] = s[n]; }
    }
    return dst;
}
int memcmp(const void *a, const void *b, size_t n) {
    const unsigned char *x = a, *y = b;
    for (size_t i = 0; i < n; ++i) if (x[i] != y[i]) return x[i] - y[i];
    return 0;
}
char *strcpy(char *dst, const char *src) {
    char *p = dst;
    do { *p++ = *src; } while (*src++);
    return dst;
}
char *strncpy(char *dst, const char *src, size_t n) {
    size_t i = 0;
    for (; i < n && src[i]; ++i) dst[i] = src[i];
    for (; i < n; ++i) dst[i] = 0;
    return dst;
}
char *strchr(const char *s, int c) {
    unsigned char ch = (unsigned char)c;
    do { if ((unsigned char)*s == ch) return (char *)s; } while (*s++);
    return NULL;
}
char *strrchr(const char *s, int c) {
    const char *found = NULL;
    unsigned char ch = (unsigned char)c;
    do { if ((unsigned char)*s == ch) found = s; } while (*s++);
    return (char *)found;
}
char *strstr(const char *s, const char *needle) {
    if (!*needle) return (char *)s;
    for (; *s; ++s) {
        size_t i = 0;
        while (needle[i] && s[i] && s[i] == needle[i]) ++i;
        if (!needle[i]) return (char *)s;
    }
    return NULL;
}
char *strdup(const char *s) {
    size_t n = strlen(s);
    if (n == SIZE_MAX) return NULL;
    char *out = malloc(n + 1);
    if (out) memcpy(out, s, n + 1);
    return out;
}
int strncasecmp(const char *a, const char *b, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        int x = tolower((unsigned char)a[i]), y = tolower((unsigned char)b[i]);
        if (x != y || !x) return x - y;
    }
    return 0;
}
int strcasecmp(const char *a, const char *b) {
    for (;;) {
        int x = tolower((unsigned char)*a++), y = tolower((unsigned char)*b++);
        if (x != y || !x) return x - y;
    }
}

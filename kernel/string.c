#include "kernel.h"

size_t strlen(const char *s) {
    size_t n = 0;
    while (s && s[n]) n++;
    return n;
}

int strcmp(const char *a, const char *b) {
    while (*a && (*a == *b)) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

int strncmp(const char *a, const char *b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (a[i] != b[i] || !a[i] || !b[i]) return (unsigned char)a[i] - (unsigned char)b[i];
    }
    return 0;
}

void *memset(void *dst, int c, size_t n) {
    unsigned char *p = dst;
    while (n--) *p++ = (unsigned char)c;
    return dst;
}

void *memcpy(void *dst, const void *src, size_t n) {
    unsigned char *d = dst;
    const unsigned char *s = src;
    while (n--) *d++ = *s++;
    return dst;
}

void strcopy(char *dst, const char *src, size_t cap) {
    if (!cap) return;
    size_t i = 0;
    for (; i + 1 < cap && src && src[i]; i++) dst[i] = src[i];
    dst[i] = '\0';
}

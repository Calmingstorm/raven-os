#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <stdint.h>
#include <inttypes.h>
#include <limits.h>
#include <errno.h>
#include <assert.h>
#include <sys/stat.h>
#include "raven_wad.h"

extern void host_check(int, const char *, int);
extern int host_vsnprintf(char *, size_t, const char *, va_list);
extern int host_boundary(void (*fn)(void), int, int);
extern int host_result(void);
extern int host_alloc_fail;
#define CHECK(expr) host_check(!!(expr), #expr, __LINE__)

static char serial[8192];
static size_t serial_count;
void vga_putc(char c) {
    CHECK(serial_count < sizeof(serial));
    if (serial_count < sizeof(serial)) serial[serial_count++] = c;
}
struct RavenWadFile { int active; };
static struct RavenWadFile handles[RAVEN_WAD_MAX_OPEN];
static const unsigned char wad_data[] = "0123456789abcdefghijklmnopqrstuvwxyz";
static int read_error;
RavenWadFile *raven_wad_open(const char *path) {
    if (strcmp(path, RAVEN_WAD_PATH) && strcmp(path, RAVEN_WAD_BASENAME)) {
        errno = ENOENT; return NULL;
    }
    for (unsigned i = 0; i < RAVEN_WAD_MAX_OPEN; ++i) if (!handles[i].active) {
        handles[i].active = 1; return &handles[i];
    }
    errno = EMFILE; return NULL;
}
int raven_wad_close(RavenWadFile *f) {
    if (!f || !f->active) { errno = EBADF; return -1; }
    f->active = 0; return 0;
}
int raven_wad_size(const RavenWadFile *f, uint32_t *out) {
    if (!f || !f->active) { errno = EBADF; return -1; }
    *out = sizeof(wad_data)-1; return 0;
}
int raven_wad_read_at(RavenWadFile *f, uint32_t off, void *dst, size_t n, size_t *got) {
    *got = 0;
    if (!f || !f->active) { errno = EBADF; return -1; }
    if (off >= sizeof(wad_data)-1) return 0;
    size_t left = sizeof(wad_data)-1-off;
    if (n > left) n = left;
    if (read_error && n > 3) n = 3;
    memcpy(dst, wad_data+off, n); *got = n;
    if (read_error) { errno = EIO; return -1; }
    return 0;
}
static void strings_test(void) {
    char b[24] = "abcdef";
    CHECK(memmove(b+1, b, 6) == b+1 && !memcmp(b, "aabcdef", 7));
    memmove(b, b+1, 6); CHECK(!memcmp(b, "abcdef", 6));
    CHECK(memmove(NULL, NULL, 0) == NULL);
    CHECK(memcmp("\x80", "\x7f", 1) > 0);
    CHECK(strcpy(b, "abc") == b && !strcmp(b, "abc"));
    memset(b, 'X', sizeof(b)); strncpy(b, "a", 4);
    CHECK(b[0] == 'a' && b[1] == 0 && b[3] == 0 && b[4] == 'X');
    strncpy(b, "abcdef", 3); CHECK(!memcmp(b, "abc", 3) && b[3] == 0);
    const char *s = "abca";
    CHECK(strchr(s, 'a') == s && strrchr(s, 'a') == s+3);
    CHECK(strchr(s, 0) == s+4 && strrchr(s, 0) == s+4);
    CHECK(!strchr(s, 'z') && !strstr("ab", "abc"));
    CHECK(strstr(s, "bc") == s+1 && strstr(s, "") == s);
    CHECK(!strcasecmp("aBc", "AbC") && !strncasecmp("ABx", "aby", 2));
    CHECK(strcasecmp("\x80", "a") > 0 && !strncasecmp(NULL, NULL, 0));
    char *copy = strdup("owned"); CHECK(copy && !strcmp(copy, "owned")); free(copy);
    host_alloc_fail = 1; CHECK(!strdup("failure")); host_alloc_fail = 0;
    CHECK(atoi(" \t-2147483648tail") == INT_MIN);
    CHECK(atoi("+2147483647") == INT_MAX && atoi("no") == 0);
    errno = 0; CHECK(atoi("999999999999999999999") == INT_MAX && errno == ERANGE);
    errno = 0; CHECK(atoi("-999999999999999999999") == INT_MIN && errno == ERANGE);
    CHECK(abs(-123) == 123 && abs(0) == 0 && getenv("HOME") == NULL);
    unsigned char *p = calloc(5, 3); CHECK(p && p[0] == 0 && p[14] == 0);
    p = realloc(p, 50); CHECK(p != NULL); free(p);
    for (int c = -1; c <= 255; ++c) {
        CHECK(!!isdigit(c) == (c >= '0' && c <= '9'));
        CHECK(!!isalpha(c) == ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')));
        CHECK(!!isalnum(c) == (!!isalpha(c) || !!isdigit(c)));
        CHECK(!!isspace(c) == (c == ' ' || (c >= 9 && c <= 13)));
        CHECK(!!isprint(c) == (c >= 32 && c <= 126));
        CHECK(tolower(c) == (c >= 'A' && c <= 'Z' ? c+32 : c));
        CHECK(toupper(c) == (c >= 'a' && c <= 'z' ? c-32 : c));
        CHECK(!!isxdigit(c) == (isdigit(c) || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f')));
    }
}
static void compare_format(const char *fmt, ...) {
    char actual[300], expected[300];
    va_list args, copy;
    va_start(args, fmt); va_copy(copy, args);
    int a = vsnprintf(actual, sizeof(actual), fmt, args);
    int e = host_vsnprintf(expected, sizeof(expected), fmt, copy);
    va_end(copy); va_end(args);
    CHECK(a == e);
    CHECK(e >= 0);
    if (e >= 0) CHECK(!memcmp(actual, expected, (size_t)e < sizeof(actual) ? (size_t)e+1 : sizeof(actual)));
}
static void format_test(void) {
    compare_format("plain %% %d %+d % d %u %#x %#X %#o", INT_MIN, 42, 42, UINT_MAX, 0xabcU, 0xabcU, 0755U);
    compare_format("%hhd %hhu %hd %hu %ld %lu %lld %llu %zd %zu", -128, 255, -32768, 65535,
                   LONG_MIN, ULONG_MAX, LLONG_MIN, ULLONG_MAX, (ptrdiff_t)-123, (size_t)123);
    compare_format("%020lld %+#020llx %-12.5d %08.4d %.0d %#.0o", LLONG_MIN, ULLONG_MAX, 23, 3, 0, 0U);
    compare_format("%*.*s/%*d/%c/%.0s", -8, 3, "abcdef", -9, 4, 'a', "hidden");
    compare_format("%.*d/%.*u", -2, 14, 6, 14U);
    compare_format("%p", (void *)(uintptr_t)0x1234);
    for (unsigned n = 0; n < 10000; n += 37) {
        compare_format("%#012x/%-8.5o/%+9d", n, n, (int)n-5000);
    }
    uint64_t random = UINT64_C(0x18ee20034c052c55);
    for (unsigned n = 0; n < 2000; ++n) {
        random ^= random << 13; random ^= random >> 7; random ^= random << 17;
        compare_format("%#025llx/%-25.23llo/%llu/%+025lld", (unsigned long long)random,
                       (unsigned long long)random, (unsigned long long)random, (long long)random);
    }
    for (int precision = -1; precision < 15; ++precision) {
        compare_format("%+#020.*x/%#020.*o/% 020.*d/%-020.*d", precision, 0U,
                       precision, 0U, precision, 0, precision, -42);
    }
    char small[5] = "xxxx";
    CHECK(snprintf(small, sizeof(small), "abcdef%d", 23) == 8 && !strcmp(small, "abcd"));
    CHECK(snprintf(small, 1, "abc") == 3 && small[0] == 0);
    CHECK(snprintf(NULL, 0, "%d", 123) == 3);
    const char no_terminator[3] = {'a', 'b', 'c'};
    CHECK(snprintf(small, sizeof(small), "%.3s", no_terminator) == 3 && !strcmp(small, "abc"));
    CHECK(snprintf(small, sizeof(small), "%2147483647d", 1) == INT_MAX && small[4] == 0);
    errno = 0; CHECK(snprintf(small, sizeof(small), "%2147483647dx", 1) == -1 && errno == EOVERFLOW);
    errno = 0; CHECK(snprintf(small, sizeof(small), "%*d", INT_MIN, 1) == -1 && errno == EOVERFLOW);
    const char *bad[] = {"%f", "%n", "%ls", "%lc", "%a", "%jX", "%tX", "%", "%1$d", "%.*c", "%#s"};
    for (size_t i = 0; i < sizeof(bad)/sizeof(bad[0]); ++i) {
        errno = 0; small[0] = 'X';
        CHECK(snprintf(small, sizeof(small), bad[i]) == -1 && errno == ENOSYS && !small[0]);
    }
    errno = 0; CHECK(snprintf(small, sizeof(small), "%999999999999d") == -1 && errno == EOVERFLOW);
    serial_count = 0; CHECK(printf("%s %d", "serial", 5) == 8);
    CHECK(serial_count == 8 && !memcmp(serial, "serial 5", 8));
    CHECK(puts("line") == 0 && putchar('x') == 'x');
    CHECK(fprintf(stderr, "%f") == -1 && errno == ENOSYS);
}
static void stdio_test(void) {
    CHECK(!fopen("unknown", "r") && errno == ENOENT);
    CHECK(!fopen(RAVEN_WAD_PATH, "wb") && errno == EROFS);
    CHECK(!fopen(RAVEN_WAD_PATH, "r+") && errno == EROFS);
    FILE *a = fopen(RAVEN_WAD_PATH, "rb"), *b = fopen(RAVEN_WAD_BASENAME, "r");
    CHECK(a && b && a != b);
    char data[64];
    CHECK(fread(data, 3, 2, a) == 2 && !memcmp(data, "012345", 6));
    CHECK(ftell(a) == 6 && ftell(b) == 0);
    CHECK(fread(NULL, 0, SIZE_MAX, a) == 0 && ftell(a) == 6);
    CHECK(fread(NULL, 1, 1, a) == 0 && errno == EINVAL && ferror(a));
    clearerr(a);
    CHECK(fseek(a, -2, SEEK_CUR) == 0 && ftell(a) == 4);
    CHECK(fseek(a, -2, SEEK_END) == 0 && ftell(a) == 34);
    CHECK(fread(data, 3, 1, a) == 0 && !memcmp(data, "yz", 2));
    CHECK(ftell(a) == 36 && feof(a) && !ferror(a));
    CHECK(fseek(a, 0, SEEK_SET) == 0 && !feof(a));
    CHECK(fseek(a, -1, SEEK_SET) == -1 && errno == EINVAL && ftell(a) == 0);
    clearerr(a); CHECK(!ferror(a));
    CHECK(fseek(a, LONG_MAX, SEEK_SET) == 0 && ftell(a) == LONG_MAX);
    CHECK(fseek(a, 1, SEEK_CUR) == -1 && errno == EOVERFLOW && ftell(a) == LONG_MAX);
    CHECK(fseek(a, LONG_MIN, SEEK_CUR) == -1 && errno == EINVAL && ftell(a) == LONG_MAX);
    clearerr(a); CHECK(fread(data, 1, 1, a) == 0 && feof(a));
    CHECK(fseek(a, 0, SEEK_SET) == 0 && !feof(a));
    CHECK(fread(data, SIZE_MAX, 2, a) == 0 && errno == EOVERFLOW && ferror(a));
    clearerr(a); read_error = 1;
    CHECK(fread(data, 2, 4, a) == 1 && ftell(a) == 3 && ferror(a) && !feof(a));
    read_error = 0; clearerr(a);
    CHECK(fwrite("x", 1, 1, a) == 0 && errno == EROFS && ferror(a));
    CHECK(fclose(a) == 0 && fclose(a) == EOF && errno == EBADF);
    CHECK(fread(data, 1, 1, a) == 0 && errno == EBADF);
    CHECK(fclose(b) == 0);
    b = fopen(RAVEN_WAD_PATH, "r");
    CHECK(fread(data, 1, 36, b) == 36 && !feof(b));
    CHECK(fread(data, 1, 1, b) == 0 && feof(b));
    clearerr(b); CHECK(!feof(b) && ftell(b) == 36);
    CHECK(fclose(b) == 0);
    FILE *all[RAVEN_WAD_MAX_OPEN];
    for (unsigned i = 0; i < RAVEN_WAD_MAX_OPEN; ++i) { all[i] = fopen(RAVEN_WAD_PATH, "r"); CHECK(all[i]); }
    CHECK(!fopen(RAVEN_WAD_PATH, "r") && errno == EMFILE);
    CHECK(!fopen("unknown", "r") && errno == ENOENT);
    raven_libc_reset_streams();
    for (unsigned i = 0; i < RAVEN_WAD_MAX_OPEN; ++i) CHECK(!handles[i].active);
    CHECK(fclose(all[0]) == EOF && errno == EBADF);
    CHECK(fflush(NULL) == 0 && fflush(stdout) == 0);
    CHECK(remove("x") == -1 && errno == EROFS);
    CHECK(rename("x", "y") == -1 && errno == EROFS);
    CHECK(mkdir("x", 0) == -1 && errno == EROFS);
    CHECK(ftell(stdout) == -1 && errno == EBADF);
    clearerr(stdout);
    CHECK(fwrite("x", SIZE_MAX, 2, stdout) == 0 && errno == EOVERFLOW && ferror(stdout));
    clearerr(stdout);
}
static void scan_test(void) {
    int d = 0, i = 0; unsigned x = 0, o = 0, u = 0;
    CHECK(sscanf(" -12 0x20 ff 17 42", "%d %i %x %o %u", &d, &i, &x, &o, &u) == 5);
    CHECK(d == -12 && i == 32 && x == 255 && o == 15 && u == 42);
    char word[5] = {0}, pair[3] = {0};
    CHECK(sscanf("skip:abcd XY", "%*4s:%4s %2c", word, pair) == 2);
    CHECK(!strcmp(word, "abcd") && !memcmp(pair, "XY", 2));
    CHECK(sscanf("% 09", "%% %i", &i) == 1 && i == 0);
    CHECK(sscanf(" \t%42", "%%%d", &d) == 1 && d == 42);
    CHECK(sscanf("", "%d", &d) == EOF && sscanf("x", "%d", &d) == 0);
    CHECK(sscanf("12x", "%d!", &d) == 1 && d == 12);
    signed char hh = 0; short h = 0; long l = 0; long long ll = 0; size_t z = 0;
    CHECK(sscanf("-128 -32768 -123 -9223372036854775808 42", "%hhd %hd %ld %lld %zu", &hh, &h, &l, &ll, &z) == 5);
    CHECK(hh == -128 && h == -32768 && l == -123 && ll == LLONG_MIN && z == 42);
    unsigned long long ull = 0;
    CHECK(sscanf("18446744073709551615", "%llu", &ull) == 1 && ull == ULLONG_MAX);
    errno = 0; d = 77;
    CHECK(sscanf("2147483648", "%d", &d) == 0 && errno == ERANGE && d == 77);
    hh = 42; errno = 0;
    CHECK(sscanf("128", "%hhd", &hh) == 0 && hh == 42 && errno == ERANGE);
    CHECK(sscanf("-129", "%hhd", &hh) == 0 && hh == 42);
    unsigned char uh = 42;
    CHECK(sscanf("255", "%hhu", &uh) == 1 && uh == 255);
    uh = 42; CHECK(sscanf("256", "%hhu", &uh) == 0 && uh == 42);
    CHECK(sscanf("-255", "%hhu", &uh) == 1 && uh == 1);
    h = 42; CHECK(sscanf("32768", "%hd", &h) == 0 && h == 42);
    unsigned short us = 42;
    CHECK(sscanf("65535", "%hu", &us) == 1 && us == 65535);
    us = 42; CHECK(sscanf("65536", "%hu", &us) == 0 && us == 42);
    ll = 42; CHECK(sscanf("9223372036854775808", "%lld", &ll) == 0 && ll == 42);
    CHECK(sscanf("-9223372036854775809", "%lld", &ll) == 0 && ll == 42);
    ull = 42; CHECK(sscanf("18446744073709551616", "%llu", &ull) == 0 && ull == 42);
    CHECK(sscanf("ffffffffffffffff", "%llx", &ull) == 1 && ull == ULLONG_MAX);
    ull = 42; CHECK(sscanf("10000000000000000", "%llx", &ull) == 0 && ull == 42);
    CHECK(sscanf("1777777777777777777777", "%llo", &ull) == 1 && ull == ULLONG_MAX);
    ull = 42; CHECK(sscanf("2000000000000000000000", "%llo", &ull) == 0 && ull == 42);
    CHECK(sscanf("-1", "%u", &u) == 1 && u == UINT_MAX);
    CHECK(sscanf("08", "%i%d", &i, &d) == 2 && i == 0 && d == 8);
    CHECK(sscanf("0x", "%i", &i) == 0);
    CHECK(sscanf("-", "%d", &i) == 0);
    CHECK(sscanf("12", "%1d%d", &i, &d) == 2 && i == 1 && d == 2);
    CHECK(sscanf("-12", "%2d%d", &i, &d) == 2 && i == -1 && d == 2);
    CHECK(sscanf("0x1", "%1x%c", &u, pair) == 2 && u == 0 && pair[0] == 'x');
    pair[0] = pair[1] = '!';
    CHECK(sscanf("a", "%2c", pair) == EOF && pair[0] == '!' && pair[1] == '!');
    CHECK(sscanf(" a", "%c", pair) == 1 && pair[0] == ' ');
    CHECK(sscanf("1 ", "%*d%d", &i) == EOF);
    CHECK(sscanf("1 2", "%*d%d", &i) == 1 && i == 2);
    CHECK(sscanf("", " ") == 0);
    i = 42; errno = 0;
    CHECK(sscanf("12", "%d%f", &i) == -1 && errno == ENOSYS && i == 42);
    char rendered[80];
    uint64_t random = UINT64_C(0x2916e41d2ac32545);
    for (unsigned n = 0; n < 3000; ++n) {
        random ^= random << 13; random ^= random >> 7; random ^= random << 17;
        snprintf(rendered, sizeof(rendered), "%llu", (unsigned long long)random);
        ull = 0; CHECK(sscanf(rendered, "%llu", &ull) == 1 && ull == random);
        snprintf(rendered, sizeof(rendered), "%#llx", (unsigned long long)random);
        ull = 0; CHECK(sscanf(rendered, "%llx", &ull) == 1 && ull == random);
        snprintf(rendered, sizeof(rendered), "%#llo", (unsigned long long)random);
        ull = 0; CHECK(sscanf(rendered, "%llo", &ull) == 1 && ull == random);
    }
    const char *bad[] = {"%f", "%n", "%[a]", "%ls", "%p", "%", "%0d"};
    for (size_t n = 0; n < sizeof(bad)/sizeof(bad[0]); ++n) {
        errno = 0; CHECK(sscanf("123", bad[n]) == -1 && errno == ENOSYS);
    }
}
static void exit_test(void) { exit(37); }
static void abort_test(void) { abort(); }
static void assert_test(void) { assert(2 + 2 == 5); }
int main(void) {
    strings_test(); format_test(); stdio_test(); scan_test();
    CHECK(host_boundary(exit_test, 1, 37));
    CHECK(host_boundary(abort_test, 2, 0));
    CHECK(host_boundary(assert_test, 2, 0));
    return host_result();
}

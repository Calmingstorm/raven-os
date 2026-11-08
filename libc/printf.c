#include <stdio.h>
#include <stdint.h>
#include <limits.h>
#include <errno.h>
#include <string.h>
#include "format_internal.h"

enum { F_MINUS = 1, F_PLUS = 2, F_SPACE = 4, F_HASH = 8, F_ZERO = 16 };
typedef struct {
    unsigned flags;
    int width, precision, width_arg, precision_arg;
    enum RavenLength length;
    char conversion;
} Spec;
typedef struct {
    char *buffer;
    size_t capacity, count;
    FILE *stream;
    int failed;
} Output;

static int number(const char **p, int *out) {
    int value = 0;
    while (**p >= '0' && **p <= '9') {
        int digit = *(*p)++ - '0';
        if (value > (INT_MAX - digit) / 10) { errno = EOVERFLOW; return -1; }
        value = value * 10 + digit;
    }
    *out = value;
    return 0;
}
static int parse(const char **p, Spec *s) {
    *s = (Spec){ .precision = -1 };
    for (;;) {
        unsigned flag;
        switch (**p) {
        case '-': flag = F_MINUS; break;
        case '+': flag = F_PLUS; break;
        case ' ': flag = F_SPACE; break;
        case '#': flag = F_HASH; break;
        case '0': flag = F_ZERO; break;
        default: flag = 0; break;
        }
        if (!flag) break;
        s->flags |= flag;
        ++*p;
    }
    if (**p == '*') { s->width_arg = 1; ++*p; }
    else if (number(p, &s->width)) return -1;
    if (**p == '.') {
        ++*p;
        s->precision = 0;
        if (**p == '*') { s->precision_arg = 1; ++*p; }
        else if (number(p, &s->precision)) return -1;
    }
    if (**p == 'h') {
        ++*p; s->length = RL_H;
        if (**p == 'h') { ++*p; s->length = RL_HH; }
    } else if (**p == 'l') {
        ++*p; s->length = RL_L;
        if (**p == 'l') { ++*p; s->length = RL_LL; }
    } else if (**p == 'z') { ++*p; s->length = RL_Z; }
    s->conversion = **p;
    if (**p) ++*p;
    switch (s->conversion) {
    case 'd': case 'i': case 'u': case 'o': case 'x': case 'X': return 0;
    case 'p':
        if (s->length == RL_NONE) return 0;
        break;
    case 's':
        if (s->length == RL_NONE && !(s->flags & ~F_MINUS)) return 0;
        break;
    case 'c':
        if (s->length == RL_NONE && s->precision < 0 && !s->precision_arg
            && !(s->flags & ~F_MINUS)) return 0;
        break;
    case '%':
        if (s->length == RL_NONE && !s->flags && !s->width && !s->width_arg
            && s->precision < 0 && !s->precision_arg) return 0;
        break;
    }
    errno = ENOSYS;
    return -1;
}
static int valid(const char *format) {
    if (!format) { errno = EINVAL; return 0; }
    while (*format) {
        if (*format++ == '%') {
            Spec s;
            if (parse(&format, &s)) return 0;
        }
    }
    return 1;
}
static void emit(Output *o, const char *s, size_t n) {
    if (o->failed) return;
    if (n > (size_t)INT_MAX - o->count) {
        errno = EOVERFLOW; o->failed = 1; return;
    }
    if (o->stream) {
        if (fwrite(s, 1, n, o->stream) != n) { o->failed = 1; return; }
    } else if (o->capacity && o->count < o->capacity - 1) {
        size_t room = o->capacity - 1 - o->count;
        memcpy(o->buffer + o->count, s, n < room ? n : room);
    }
    o->count += n;
}
static void repeat(Output *o, char c, size_t n) {
    if (o->failed) return;
    if (n > (size_t)INT_MAX - o->count) {
        errno = EOVERFLOW; o->failed = 1; return;
    }
    if (!o->stream) {
        if (o->capacity && o->count < o->capacity - 1) {
            size_t room = o->capacity - 1 - o->count;
            memset(o->buffer + o->count, c, n < room ? n : room);
        }
        o->count += n;
    } else {
        char block[32];
        memset(block, c, sizeof(block));
        while (n && !o->failed) {
            size_t step = n < sizeof(block) ? n : sizeof(block);
            emit(o, block, step); n -= step;
        }
    }
}
static int format_to(Output *o, const char *format, va_list args) {
    /* Validate the entire format before reading any variadic argument. */
    if (!valid(format)) { o->failed = 1; return -1; }
    while (*format && !o->failed) {
        if (*format != '%') {
            const char *start = format;
            while (*format && *format != '%') ++format;
            emit(o, start, (size_t)(format - start));
            continue;
        }
        ++format;
        Spec s;
        if (parse(&format, &s)) { o->failed = 1; break; }
        if (s.width_arg) {
            s.width = va_arg(args, int);
            if (s.width < 0) {
                if (s.width == INT_MIN) { errno = EOVERFLOW; o->failed = 1; break; }
                s.width = -s.width; s.flags |= F_MINUS;
            }
        }
        if (s.precision_arg) {
            s.precision = va_arg(args, int);
            if (s.precision < 0) s.precision = -1;
        }
        if (s.conversion == '%') { emit(o, "%", 1); continue; }
        if (s.conversion == 's' || s.conversion == 'c') {
            char ch;
            const char *text;
            size_t n = 0;
            if (s.conversion == 'c') {
                ch = (char)va_arg(args, int); text = &ch; n = 1;
            } else {
                text = va_arg(args, const char *);
                if (!text) text = "(null)";
                while ((s.precision < 0 || n < (size_t)s.precision) && text[n]) ++n;
            }
            size_t pad = (size_t)s.width > n ? (size_t)s.width - n : 0;
            if (!(s.flags & F_MINUS)) repeat(o, ' ', pad);
            emit(o, text, n);
            if (s.flags & F_MINUS) repeat(o, ' ', pad);
            continue;
        }
        uint64_t value;
        int negative = 0;
        int signed_conversion = s.conversion == 'd' || s.conversion == 'i';
        if (s.conversion == 'p') value = (uintptr_t)va_arg(args, void *);
        else if (signed_conversion) {
            int64_t v;
            switch (s.length) {
            case RL_HH: v = (signed char)va_arg(args, int); break;
            case RL_H: v = (short)va_arg(args, int); break;
            case RL_L: v = va_arg(args, long); break;
            case RL_LL: v = va_arg(args, long long); break;
            case RL_Z: v = va_arg(args, __PTRDIFF_TYPE__); break;
            default: v = va_arg(args, int); break;
            }
            negative = v < 0;
            value = negative ? UINT64_C(0) - (uint64_t)v : (uint64_t)v;
        } else {
            switch (s.length) {
            case RL_HH: value = (unsigned char)va_arg(args, int); break;
            case RL_H: value = (unsigned short)va_arg(args, int); break;
            case RL_L: value = va_arg(args, unsigned long); break;
            case RL_LL: value = va_arg(args, unsigned long long); break;
            case RL_Z: value = va_arg(args, size_t); break;
            default: value = va_arg(args, unsigned int); break;
            }
        }
        unsigned base = s.conversion == 'o' ? 8 :
            (s.conversion == 'x' || s.conversion == 'X' || s.conversion == 'p') ? 16 : 10;
        char digits[64], prefix[3];
        size_t n = 0, pn = 0;
        int nonzero = value != 0;
        const char *alphabet = s.conversion == 'X' ? "0123456789ABCDEF" : "0123456789abcdef";
        if (value || s.precision != 0 || s.conversion == 'p') {
            do {
                unsigned rem;
                value = raven_divsmall(value, base, &rem);
                digits[n++] = alphabet[rem];
            } while (value);
        }
        if (signed_conversion) {
            if (negative) prefix[pn++] = '-';
            else if (s.flags & F_PLUS) prefix[pn++] = '+';
            else if (s.flags & F_SPACE) prefix[pn++] = ' ';
        }
        if (s.conversion == 'p' || ((s.flags & F_HASH) && base == 16 && nonzero)) {
            prefix[pn++] = '0'; prefix[pn++] = s.conversion == 'X' ? 'X' : 'x';
        }
        size_t zeros = s.precision > 0 && (size_t)s.precision > n ? (size_t)s.precision - n : 0;
        if ((s.flags & F_HASH) && base == 8 && !zeros && (!n || digits[n-1] != '0')) ++zeros;
        size_t total = pn + zeros + n;
        size_t pad = (size_t)s.width > total ? (size_t)s.width - total : 0;
        if (!(s.flags & F_MINUS) && !((s.flags & F_ZERO) && s.precision < 0)) repeat(o, ' ', pad);
        emit(o, prefix, pn);
        if (!(s.flags & F_MINUS) && (s.flags & F_ZERO) && s.precision < 0) repeat(o, '0', pad);
        repeat(o, '0', zeros);
        while (n) emit(o, &digits[--n], 1);
        if (s.flags & F_MINUS) repeat(o, ' ', pad);
    }
    return o->failed ? -1 : (int)o->count;
}
int vsnprintf(char *dst, size_t capacity, const char *format, va_list args) {
    if (!dst && capacity) { errno = EINVAL; return -1; }
    Output o = { .buffer = dst, .capacity = capacity };
    va_list copy;
    va_copy(copy, args);
    int result = format_to(&o, format, copy);
    va_end(copy);
    if (capacity) dst[o.count < capacity ? o.count : capacity - 1] = 0;
    return result;
}
int snprintf(char *dst, size_t capacity, const char *format, ...) {
    va_list args; va_start(args, format);
    int result = vsnprintf(dst, capacity, format, args);
    va_end(args); return result;
}
int vfprintf(FILE *stream, const char *format, va_list args) {
    if (!stream) { errno = EBADF; return -1; }
    Output o = { .stream = stream };
    va_list copy; va_copy(copy, args);
    int result = format_to(&o, format, copy);
    va_end(copy); return result;
}
int fprintf(FILE *stream, const char *format, ...) {
    va_list args; va_start(args, format);
    int result = vfprintf(stream, format, args);
    va_end(args); return result;
}
int vprintf(const char *format, va_list args) { return vfprintf(stdout, format, args); }
int printf(const char *format, ...) {
    va_list args; va_start(args, format);
    int result = vprintf(format, args);
    va_end(args); return result;
}

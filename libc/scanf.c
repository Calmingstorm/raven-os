#include <stdio.h>
#include <stdint.h>
#include <limits.h>
#include <errno.h>

/* Deliberately small, integer-only scanf.  Validate the ENTIRE format before
 * starting the va_list or touching input/destinations.  In particular, %n,
 * floats, scansets, positional arguments and wide characters are not accepted.
 * Malformed formats are rejected by the same ENOSYS/-1 policy.
 */
enum scan_length { SC_DEFAULT, SC_HH, SC_H, SC_L, SC_LL, SC_Z };
struct scan_spec {
    size_t width;
    int suppressed;
    enum scan_length length;
    char conversion;
};

static int scan_space(unsigned char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' ||
           c == '\f' || c == '\v';
}

/* *format points just after '%'.  There are no reads past a terminating NUL. */
static int scan_specification(const char **format, struct scan_spec *spec) {
    const char *p = *format;
    int has_width = 0;
    spec->width = 0;
    spec->suppressed = 0;
    spec->length = SC_DEFAULT;
    if (*p == '*') { spec->suppressed = 1; ++p; }
    while (*p >= '0' && *p <= '9') {
        unsigned digit = (unsigned)(*p++ - '0');
        has_width = 1;
        if (spec->width > (SIZE_MAX - digit) / 10U) return 0;
        spec->width = spec->width * 10U + digit;
    }
    if (has_width && spec->width == 0) return 0;
    if (*p == 'h') {
        ++p;
        spec->length = SC_H;
        if (*p == 'h') { ++p; spec->length = SC_HH; }
    } else if (*p == 'l') {
        ++p;
        spec->length = SC_L;
        if (*p == 'l') { ++p; spec->length = SC_LL; }
    } else if (*p == 'z') {
        ++p;
        spec->length = SC_Z;
    }
    spec->conversion = *p;
    switch (*p) {
    case 'd': case 'i': case 'u': case 'o': case 'x': case 'X':
        break;
    case 'c': case 's':
        if (spec->length != SC_DEFAULT) return 0;
        break;
    case '%':
        if (has_width || spec->suppressed || spec->length != SC_DEFAULT)
            return 0;
        break;
    default:
        return 0;
    }
    if (!has_width) spec->width = *p == 'c' ? 1 : SIZE_MAX;
    *format = p + 1;
    return 1;
}

static int scan_format_valid(const char *format) {
    struct scan_spec spec;
    while (*format) {
        if (*format++ == '%' && !scan_specification(&format, &spec))
            return 0;
    }
    return 1;
}

/* A small-divisor binary long division, avoiding i386 __udivdi3/__umoddi3.
 * remainder stays below 2*base, and all shifts are unsigned and in range.
 */
static uint64_t scan_cutoff(uint64_t limit, unsigned base, unsigned *remainder) {
    uint64_t quotient = 0;
    unsigned rem = 0;
    unsigned bit = 64;
    while (bit) {
        --bit;
        rem = (rem << 1) | (unsigned)((limit >> bit) & 1U);
        quotient <<= 1;
        if (rem >= base) { rem -= base; quotient |= 1U; }
    }
    *remainder = rem;
    return quotient;
}

static int scan_digit(unsigned char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static uint64_t scan_limit(enum scan_length length, int is_signed,
                           int negative) {
    uint64_t limit;
    if (is_signed) {
        switch (length) {
        case SC_HH: limit = SCHAR_MAX; break;
        case SC_H: limit = SHRT_MAX; break;
        case SC_L: limit = LONG_MAX; break;
        case SC_LL: limit = LLONG_MAX; break;
        /* On both Raven i386 and the native LP64 test ABI, ptrdiff_t is
         * the signed integer type corresponding to size_t.
         */
        case SC_Z: limit = PTRDIFF_MAX; break;
        default: limit = INT_MAX; break;
        }
        return limit + (negative ? 1U : 0U);
    }
    switch (length) {
    case SC_HH: return UCHAR_MAX;
    case SC_H: return USHRT_MAX;
    case SC_L: return ULONG_MAX;
    case SC_LL: return ULLONG_MAX;
    case SC_Z: return SIZE_MAX;
    default: return UINT_MAX;
    }
}

static void scan_store(va_list *args, enum scan_length length, int is_signed,
                       int negative, uint64_t magnitude) {
    if (is_signed) {
        /* Do not cast 2^63 to signed or negate INT64_MIN. */
        int64_t value = negative && magnitude
                      ? -(int64_t)(magnitude - 1U) - 1
                      : (int64_t)magnitude;
        switch (length) {
        case SC_HH: *va_arg(*args, signed char *) = (signed char)value; break;
        case SC_H: *va_arg(*args, short *) = (short)value; break;
        case SC_L: *va_arg(*args, long *) = (long)value; break;
        case SC_LL: *va_arg(*args, long long *) = (long long)value; break;
        case SC_Z: *va_arg(*args, ptrdiff_t *) = (ptrdiff_t)value; break;
        default: *va_arg(*args, int *) = (int)value; break;
        }
    } else {
        uint64_t value = negative ? (uint64_t)0 - magnitude : magnitude;
        switch (length) {
        case SC_HH: *va_arg(*args, unsigned char *) = (unsigned char)value; break;
        case SC_H: *va_arg(*args, unsigned short *) = (unsigned short)value; break;
        case SC_L: *va_arg(*args, unsigned long *) = (unsigned long)value; break;
        case SC_LL: *va_arg(*args, unsigned long long *) = (unsigned long long)value; break;
        case SC_Z: *va_arg(*args, size_t *) = (size_t)value; break;
        default: *va_arg(*args, unsigned *) = (unsigned)value; break;
        }
    }
}

int sscanf(const char *src, const char *format, ...) {
    va_list args;
    int assignments = 0;
    int result;
    if (!scan_format_valid(format)) { errno = ENOSYS; return -1; }
    va_start(args, format);
    while (*format) {
        struct scan_spec spec;
        if (scan_space((unsigned char)*format)) {
            do { ++format; } while (scan_space((unsigned char)*format));
            while (scan_space((unsigned char)*src)) ++src;
            continue;
        }
        if (*format != '%') {
            if (!*src) goto input_failure;
            if (*src != *format) goto matching_failure;
            ++src;
            ++format;
            continue;
        }
        ++format;
        (void)scan_specification(&format, &spec); /* Already validated. */
        if (spec.conversion == '%') {
            while (scan_space((unsigned char)*src)) ++src;
            if (!*src) goto input_failure;
            if (*src != '%') goto matching_failure;
            ++src;
            continue;
        }
        if (spec.conversion != 'c')
            while (scan_space((unsigned char)*src)) ++src;
        if (!*src) goto input_failure;
        if (spec.conversion == 'c' || spec.conversion == 's') {
            size_t count = 0;
            const char *start = src;
            while (count < spec.width && *src &&
                   (spec.conversion == 'c' || !scan_space((unsigned char)*src))) {
                ++src;
                ++count;
            }
            /* %c is an exact-width conversion; no partial destination write. */
            if (spec.conversion == 'c' && count != spec.width) goto input_failure;
            if (!spec.suppressed) {
                char *dst = va_arg(args, char *);
                size_t i;
                for (i = 0; i < count; ++i) dst[i] = start[i];
                if (spec.conversion == 's') dst[count] = '\0';
                ++assignments;
            }
        } else {
            unsigned base = spec.conversion == 'o' ? 8U :
                            (spec.conversion == 'x' || spec.conversion == 'X') ? 16U : 10U;
            int is_signed = spec.conversion == 'd' || spec.conversion == 'i';
            int negative = 0, overflow = 0, any = 0;
            size_t left = spec.width;
            uint64_t value = 0, limit, cutoff;
            unsigned remainder;
            if (*src == '-' || *src == '+') {
                negative = *src == '-';
                ++src;
                --left;
            }
            /* Prefix bytes count toward width.  A bare sign or 0x prefix is
             * a matching failure, not a successful zero conversion.
             */
            if (left && *src == '0') {
                if (spec.conversion == 'i') base = 8;
                if ((spec.conversion == 'i' || base == 16) && left > 1 &&
                    (src[1] == 'x' || src[1] == 'X')) {
                    base = 16;
                    src += 2;
                    left -= 2;
                }
            }
            limit = scan_limit(spec.length, is_signed, negative);
            cutoff = scan_cutoff(limit, base, &remainder);
            while (left && *src) {
                int digit = scan_digit((unsigned char)*src);
                if (digit < 0 || (unsigned)digit >= base) break;
                any = 1;
                if (value > cutoff || (value == cutoff && (unsigned)digit > remainder))
                    overflow = 1;
                else if (!overflow) {
                    /* Bounds checked before multiply/add, so these shifts and
                     * sums cannot wrap. No 64-bit division or multiply helper.
                     */
                    if (base == 16) value <<= 4;
                    else if (base == 8) value <<= 3;
                    else value = (value << 3) + (value << 1);
                    value += (unsigned)digit;
                }
                ++src;
                --left;
            }
            if (!any) goto matching_failure;
            /* Suppressed integer conversions still check the selected type's
             * range, but never retrieve or write a destination argument.
             */
            if (overflow) { errno = ERANGE; goto matching_failure; }
            if (!spec.suppressed) {
                scan_store(&args, spec.length, is_signed, negative, value);
                ++assignments;
            }
        }
    }
matching_failure:
    result = assignments;
    goto done;
input_failure:
    result = assignments ? assignments : EOF;
done:
    va_end(args);
    return result;
}

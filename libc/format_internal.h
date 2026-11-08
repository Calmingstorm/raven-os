#ifndef RAVEN_FORMAT_INTERNAL_H
#define RAVEN_FORMAT_INTERNAL_H
#include <stdint.h>
enum RavenLength { RL_NONE, RL_HH, RL_H, RL_L, RL_LL, RL_Z };
/* No __udivdi3 dependency on i386. Remainder < 16 at every step. */
static inline uint64_t raven_divsmall(uint64_t value, unsigned base,
                                      unsigned *remainder) {
    uint64_t quotient = 0;
    unsigned rem = 0;
    for (unsigned bit = 64; bit; ) {
        --bit;
        rem = (rem << 1) | (unsigned)((value >> bit) & 1U);
        if (rem >= base) { rem -= base; quotient |= UINT64_C(1) << bit; }
    }
    *remainder = rem;
    return quotient;
}
#endif

#include <stddef.h>
#include "../libc/include/setjmp.h"

_Static_assert(sizeof(jmp_buf) == 24, "i386 jmp_buf layout");
_Static_assert(offsetof(RavenJmpState, ebx) == 0, "ebx offset");
_Static_assert(offsetof(RavenJmpState, esi) == 4, "esi offset");
_Static_assert(offsetof(RavenJmpState, edi) == 8, "edi offset");
_Static_assert(offsetof(RavenJmpState, ebp) == 12, "ebp offset");
_Static_assert(offsetof(RavenJmpState, esp) == 16, "esp offset");
_Static_assert(offsetof(RavenJmpState, eip) == 20, "eip offset");

static jmp_buf outer, inner;
static volatile int phase;

__attribute__((noinline)) static void jump_deep(int value) {
    volatile unsigned local[64];
    for (unsigned i = 0; i < 64; ++i) local[i] = i;
    if (local[63] == 63) longjmp(inner, value);
}

int setjmp_c_test(void) {
    volatile int value;
    /* Calls appear in valid setjmp controlling expressions. Modified locals
     * needed after longjmp are volatile, per the C language contract.
     */
    for (value = -2; value <= 2; ++value) {
        phase = 0;
        if (setjmp(outer) == 0) {
            if (setjmp(inner) == 0) {
                if (phase != 0) return 2;
                phase = 1;
                jump_deep(value);
                return 3;
            } else {
                if (phase != 1) return 1;
                phase = 2;
                longjmp(outer, 7);
            }
        } else if (phase != 2) return 4;
    }
    return 0;
}

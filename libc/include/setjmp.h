#ifndef RAVEN_LIBC_SETJMP_H
#define RAVEN_LIBC_SETJMP_H

#include <stdint.h>

/* i386 SysV integer state only, not signal masks or FPU state. Both sides
 * must be in main-loop context with IF enabled, never IRQ/exception code.
 * The saved frame must still be live when longjmp is called.
 */
typedef struct {
    uint32_t ebx, esi, edi, ebp, esp, eip;
} RavenJmpState;
typedef RavenJmpState jmp_buf[1];

int setjmp(jmp_buf env) __attribute__((returns_twice));
_Noreturn void longjmp(jmp_buf env, int value);

#endif

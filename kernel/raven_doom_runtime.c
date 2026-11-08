#include "kernel.h"
#include "raven_doom_runtime.h"
#include "raven_wad.h"
#include "../libc/include/errno.h"
#ifdef RAVEN_RUNTIME_TEST
#include <setjmp.h>
#else
#include "../libc/include/setjmp.h"
#endif

/* All mutable post-setjmp state is static: longjmp must not observe clobbered
 * non-volatile automatic locals. No IRQ calls into this main-loop service. */
typedef struct Allocation {
    struct Allocation *next, *prev;
    size_t size;
} Allocation;
#define ALLOC_HEADER ((sizeof(Allocation) + 15u) & ~(size_t)15u)
static Allocation *allocations;
static size_t allocation_count;
static jmp_buf boundary;
static RavenDoomResult result;
static char abort_reason[160];
static unsigned phase; /* 0 idle, 1 entry, 2 cleanup */
static RavenDoomCleanup session_cleanup;
extern void raven_libc_reset_streams(void);

#ifdef RAVEN_RUNTIME_TEST
extern int raven_runtime_interrupts_enabled(void);
#else
static int raven_runtime_interrupts_enabled(void) {
    uint32_t flags;
    __asm__ volatile ("pushfl; popl %0" : "=r"(flags));
    return (flags & (1u << 9)) != 0;
}
#endif

static _Noreturn void programming_fault(const char *reason) {
    serial_write("FATAL Doom runtime misuse: ");
    serial_write(reason);
    serial_write("\n");
#ifdef RAVEN_RUNTIME_TEST
    __builtin_trap();
#else
    __asm__ volatile ("cli" ::: "memory");
    for (;;) __asm__ volatile ("hlt");
#endif
}

int raven_doom_active(void) { return phase != 0; }
size_t raven_doom_allocation_count(void) { return allocation_count; }
const char *raven_doom_abort_reason(void) { return abort_reason; }

static Allocation *find_allocation(void *ptr) {
    for (Allocation *a = allocations; a; a = a->next)
        if ((unsigned char *)a + ALLOC_HEADER == ptr) return a;
    return 0;
}

void *raven_doom_malloc(size_t size) {
    if (!phase) { errno = EINVAL; return 0; }
    if (!size) return 0;
    if (size > SIZE_MAX - ALLOC_HEADER) { errno = ENOMEM; return 0; }
    Allocation *a = kmalloc(size + ALLOC_HEADER);
    if (!a) { errno = ENOMEM; return 0; }
    a->size = size;
    a->prev = 0;
    a->next = allocations;
    if (allocations) allocations->prev = a;
    allocations = a;
    allocation_count++;
    return (unsigned char *)a + ALLOC_HEADER;
}

void raven_doom_free(void *ptr) {
    if (!ptr) return;
    Allocation *a = find_allocation(ptr);
    if (!a) { errno = EINVAL; return; }
    if (a->prev) a->prev->next = a->next;
    else allocations = a->next;
    if (a->next) a->next->prev = a->prev;
    allocation_count--;
    kfree(a);
}

void *raven_doom_calloc(size_t count, size_t size) {
    if (size && count > SIZE_MAX / size) { errno = ENOMEM; return 0; }
    void *p = raven_doom_malloc(count * size);
    if (p) memset(p, 0, count * size);
    return p;
}

void *raven_doom_realloc(void *ptr, size_t size) {
    if (!ptr) return raven_doom_malloc(size);
    Allocation *a = find_allocation(ptr);
    if (!a) { errno = EINVAL; return 0; }
    if (!size) { raven_doom_free(ptr); return 0; }
    /* Allocate first: failure must leave the old pointer and its contents. */
    void *p = raven_doom_malloc(size);
    if (!p) return 0;
    memcpy(p, ptr, size < a->size ? size : a->size);
    raven_doom_free(ptr);
    return p;
}

static void discard_input(void) {
    KeyEvent event;
    /* IRQs may keep arriving; never let cleanup become an unbounded loop. */
    for (unsigned i = 0; i < KEYBUF_CAP && keyboard_pop(&event); i++) {}
    char c;
    for (unsigned i = 0; i < KEYBUF_CAP && serial_read_char(&c); i++) {}
}

RavenDoomResult raven_doom_run(RavenDoomEntry entry, RavenDoomCleanup cleanup,
                              int argc, char **argv) {
    if (phase) return (RavenDoomResult){ RAVEN_DOOM_BUSY, 0 };
    if (!entry || argc < 0 || (argc && !argv) || !raven_runtime_interrupts_enabled())
        return (RavenDoomResult){ RAVEN_DOOM_UNAVAILABLE, EINVAL };
    for (int i = 0; i < argc; i++)
        if (!argv[i]) return (RavenDoomResult){ RAVEN_DOOM_UNAVAILABLE, EINVAL };
    session_cleanup = cleanup;
    result = (RavenDoomResult){ RAVEN_DOOM_RETURNED, 0 };
    abort_reason[0] = '\0';
    discard_input();
    phase = 1;
    if (setjmp(boundary) == 0) entry(argc, argv);
    phase = 2;
    if (session_cleanup) session_cleanup();
    raven_libc_reset_streams();
    raven_wad_unmount();
    while (allocations)
        raven_doom_free((unsigned char *)allocations + ALLOC_HEADER);
    discard_input();
    phase = 0;
    session_cleanup = 0;
    return result;
}

_Noreturn void raven_doom_exit(int status) {
    if (phase != 1) programming_fault("exit outside entry");
    result = (RavenDoomResult){ RAVEN_DOOM_EXITED, status };
    longjmp(boundary, 1);
}

_Noreturn void raven_doom_abort(const char *reason) {
    if (phase != 1) programming_fault("abort outside entry");
    if (!reason || !reason[0]) reason = "abort";
    /* Own the snapshot before longjmp, port cleanup or session reclamation can
     * invalidate its source. Leave room for a visible truncation marker. */
    size_t i = 0;
    while (i < sizeof(abort_reason) - 1 && reason[i]) {
        abort_reason[i] = reason[i];
        i++;
    }
    abort_reason[i] = '\0';
    if (reason[i]) {
        abort_reason[i - 3] = '.';
        abort_reason[i - 2] = '.';
        abort_reason[i - 1] = '.';
    }
    /* Serial only: a partially initialized port may own the VGA registers. */
    serial_write("Doom session error: ");
    serial_write(reason);
    serial_write("\n");
    result = (RavenDoomResult){ RAVEN_DOOM_ABORTED, 1 };
    longjmp(boundary, 1);
}

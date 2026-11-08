#define _DEFAULT_SOURCE
#include <limits.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
#include "../kernel/kernel.h"
#include "../kernel/raven_doom_runtime.h"
#include "../libc/include/errno.h"

/* Deliberately do not put libc/include on the host include path. In particular
 * the runtime uses the host setjmp ABI under RAVEN_RUNTIME_TEST. */
int errno;
static unsigned checks;
#define CHECK(expr) do { checks++; if (!(expr)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr); exit(1); \
} } while (0)

typedef struct { void *ptr; size_t size; } HostAllocation;
static HostAllocation host_allocations[128];
static unsigned host_live, alloc_calls, free_calls;
static int fail_next_allocation, interrupts_enabled = 1;
static unsigned entry_calls, cleanup_calls, reset_calls, unmount_calls;
static unsigned keyboard_calls, serial_calls, keyboard_pending, serial_pending;
static int infinite_input, streams_live, expected_cleanup, entry_started;
static int expected_argc, exit_status, action, cleanup_exercises;
static char **expected_argv;
static unsigned char *session_memory, *unrelated;
static char serial_log[1024];
static unsigned vga_calls;
static void *inaccessible;

void *kmalloc(size_t size) {
    alloc_calls++;
    if (fail_next_allocation) { fail_next_allocation = 0; return NULL; }
    /* Huge requests need not be passed to ASan's host allocator. */
    if (size > 4u * 1024u * 1024u) return NULL;
    void *p = malloc(size);
    CHECK(p != NULL);
    CHECK(((uintptr_t)p & 15u) == 0);
    for (unsigned i = 0; i < 128; i++) if (!host_allocations[i].ptr) {
        host_allocations[i] = (HostAllocation){p, size};
        host_live++;
        return p;
    }
    CHECK(0 && "host allocation registry full");
    return NULL;
}

void kfree(void *ptr) {
    CHECK(ptr != NULL);
    for (unsigned i = 0; i < 128; i++) if (host_allocations[i].ptr == ptr) {
        /* Cleanup must precede automatic reclamation, but explicit entry and
         * cleanup frees are permitted. Host ASan then catches any late access. */
        free(ptr);
        host_allocations[i].ptr = NULL;
        host_live--;
        free_calls++;
        return;
    }
    CHECK(0 && "invalid/double kernel free");
}

int raven_runtime_interrupts_enabled(void) { return interrupts_enabled; }
int keyboard_pop(KeyEvent *out) {
    keyboard_calls++;
    if (!infinite_input && !keyboard_pending) return 0;
    if (keyboard_pending) keyboard_pending--;
    *out = (KeyEvent){.code = KEY_CHARACTER, .ascii = 'q', .pressed = 1};
    return 1;
}
int serial_read_char(char *out) {
    serial_calls++;
    if (!infinite_input && !serial_pending) return 0;
    if (serial_pending) serial_pending--;
    *out = 'q';
    return 1;
}
void serial_write(const char *s) {
    CHECK(strlen(serial_log) + strlen(s) < sizeof serial_log);
    strcat(serial_log, s);
}
void vga_write(const char *s) { (void)s; vga_calls++; }
void vga_putc(char c) { (void)c; vga_calls++; }
void vga_write_dec(uint32_t n) { (void)n; vga_calls++; }

void raven_libc_reset_streams(void) {
    CHECK(raven_doom_active());
    CHECK(entry_started && streams_live);
    CHECK(cleanup_calls == (unsigned)expected_cleanup);
    if (session_memory) CHECK(session_memory[0] == 0x5a);
    CHECK(unmount_calls == 0);
    streams_live = 0;
    reset_calls++;
}
void raven_wad_unmount(void) {
    CHECK(raven_doom_active());
    CHECK(reset_calls == 1 && !streams_live);
    if (session_memory) CHECK(session_memory[0] == 0x5a);
    unmount_calls++;
}

static void never_entry(int argc, char **argv) {
    (void)argc; (void)argv;
    CHECK(0 && "rejected entry ran");
}
static void never_cleanup(void) { CHECK(0 && "rejected cleanup ran"); }

static void check_nested_busy(void) {
    size_t before = raven_doom_allocation_count();
    char reason_before[160];
    strcpy(reason_before, raven_doom_abort_reason());
    unsigned resets = reset_calls, unmounts = unmount_calls;
    RavenDoomResult nested = raven_doom_run(never_entry, never_cleanup, 0, NULL);
    CHECK(nested.kind == RAVEN_DOOM_BUSY);
    /* Busy is checked before argument/IF preflight and cannot destroy the
     * outer session, replace its cleanup, or install a new jump boundary. */
    interrupts_enabled = 0;
    nested = raven_doom_run(NULL, never_cleanup, -1, NULL);
    interrupts_enabled = 1;
    CHECK(nested.kind == RAVEN_DOOM_BUSY);
    CHECK(raven_doom_allocation_count() == before);
    CHECK(reset_calls == resets && unmount_calls == unmounts);
    CHECK(strcmp(reason_before, raven_doom_abort_reason()) == 0);
    CHECK(raven_doom_active());
}

static void allocation_exercises(void) {
    size_t baseline = raven_doom_allocation_count();
    unsigned calls = alloc_calls;
    CHECK(raven_doom_malloc(0) == NULL);
    CHECK(raven_doom_calloc(0, SIZE_MAX) == NULL);
    CHECK(raven_doom_calloc(SIZE_MAX, 0) == NULL);
    CHECK(alloc_calls == calls);
    CHECK(raven_doom_malloc(SIZE_MAX) == NULL && errno == ENOMEM);
    CHECK(raven_doom_calloc(SIZE_MAX / 2 + 1, 2) == NULL && errno == ENOMEM);
    CHECK(alloc_calls == calls);

    unsigned char *p = raven_doom_calloc(19, 3);
    CHECK(p && ((uintptr_t)p & 15u) == 0);
    for (size_t i = 0; i < 57; i++) CHECK(p[i] == 0);
    memset(p, 0x36, 57);
    size_t count = raven_doom_allocation_count();
    fail_next_allocation = 1;
    CHECK(raven_doom_realloc(p, 128) == NULL && errno == ENOMEM);
    CHECK(raven_doom_allocation_count() == count);
    for (size_t i = 0; i < 57; i++) CHECK(p[i] == 0x36);
    CHECK(raven_doom_realloc(p, SIZE_MAX) == NULL && errno == ENOMEM);
    CHECK(raven_doom_allocation_count() == count && p[56] == 0x36);
    p = raven_doom_realloc(p, 128);
    CHECK(p && ((uintptr_t)p & 15u) == 0);
    for (size_t i = 0; i < 57; i++) CHECK(p[i] == 0x36);
    CHECK(raven_doom_allocation_count() == count);
    p = raven_doom_realloc(p, 7);
    CHECK(p && ((uintptr_t)p & 15u) == 0);
    for (size_t i = 0; i < 7; i++) CHECK(p[i] == 0x36);
    CHECK(raven_doom_realloc(p, 0) == NULL);
    CHECK(raven_doom_allocation_count() == baseline);

    p = raven_doom_realloc(NULL, 31);
    CHECK(p && ((uintptr_t)p & 15u) == 0);
    unsigned frees = free_calls;
    int stack_value = 1;
    void *bad[] = {inaccessible, (void *)(uintptr_t)1, &stack_value,
                   unrelated, p + 1};
    for (size_t i = 0; i < sizeof bad / sizeof *bad; i++) {
        errno = 0;
        raven_doom_free(bad[i]);
        CHECK(errno == EINVAL);
        CHECK(raven_doom_realloc(bad[i], 8) == NULL && errno == EINVAL);
        CHECK(raven_doom_realloc(bad[i], 0) == NULL && errno == EINVAL);
    }
    CHECK(free_calls == frees);
    raven_doom_free(p);
    errno = 0;
    raven_doom_free(p); /* stale pointer comparison is safe, never dereference */
    CHECK(errno == EINVAL);
    CHECK(raven_doom_realloc(p, 8) == NULL && errno == EINVAL);
    raven_doom_free(NULL);
    CHECK(raven_doom_allocation_count() == baseline);

    /* Exercise head, tail and middle removal, including one-byte payloads. */
    void *blocks[17];
    for (unsigned i = 0; i < 17; i++) {
        blocks[i] = raven_doom_malloc(i + 1);
        CHECK(blocks[i] && ((uintptr_t)blocks[i] & 15u) == 0);
        memset(blocks[i], (int)i, i + 1);
    }
    raven_doom_free(blocks[0]);
    raven_doom_free(blocks[8]);
    raven_doom_free(blocks[16]);
    for (unsigned i = 1; i < 16; i++) if (i != 8) raven_doom_free(blocks[i]);
    CHECK(raven_doom_allocation_count() == baseline);
}

static void cleanup(void) {
    CHECK(cleanup_calls++ == 0);
    CHECK(raven_doom_active() && streams_live);
    CHECK(reset_calls == 0 && unmount_calls == 0);
    CHECK(raven_doom_allocation_count() >= 1);
    CHECK(session_memory && session_memory[0] == 0x5a && session_memory[32] == 0x5a);
    CHECK(unrelated[0] == 0x73);
    check_nested_busy();
    if (cleanup_exercises) {
        allocation_exercises();
        /* A cleanup may grow an entry allocation, release it, and allocate
         * something new for automatic reclamation after streams close. */
        session_memory = raven_doom_realloc(session_memory, 96);
        CHECK(session_memory && session_memory[32] == 0x5a);
        raven_doom_free(session_memory);
        session_memory = raven_doom_malloc(33);
        CHECK(session_memory != NULL);
        memset(session_memory, 0x5a, 33);
        CHECK(raven_doom_malloc(11) != NULL);
    }
    /* Pending game input must not become the shell's next command. */
    keyboard_pending = KEYBUF_CAP - 1;
    serial_pending = KEYBUF_CAP - 1;
}

static void entry(int argc, char **argv) {
    CHECK(entry_calls++ == 0);
    CHECK(raven_doom_abort_reason()[0] == 0);
    entry_started = 1;
    CHECK(raven_doom_active());
    CHECK(argc == expected_argc && argv == expected_argv);
    CHECK(!keyboard_pending && !serial_pending);
    CHECK(!cleanup_calls && !reset_calls && !unmount_calls);
    streams_live = 1;
    session_memory = raven_doom_malloc(33);
    CHECK(session_memory && ((uintptr_t)session_memory & 15u) == 0);
    memset(session_memory, 0x5a, 33);
    check_nested_busy();
    allocation_exercises();
    /* Any allocations left by entry, including on abort, belong to runtime. */
    CHECK(raven_doom_malloc(123) != NULL);
    keyboard_pending = KEYBUF_CAP - 1;
    serial_pending = KEYBUF_CAP - 1;
    if (action == 1) raven_doom_exit(exit_status);
    if (action == 2) raven_doom_abort("partial graphics initialization failed");
    if (action == 3) raven_doom_abort(NULL);
}

static void prepare(void) {
    CHECK(!raven_doom_active());
    CHECK(raven_doom_allocation_count() == 0);
    CHECK(host_live == 1 && unrelated[0] == 0x73);
    entry_calls = cleanup_calls = reset_calls = unmount_calls = 0;
    keyboard_calls = serial_calls = 0;
    keyboard_pending = serial_pending = KEYBUF_CAP - 1;
    session_memory = NULL;
    streams_live = entry_started = 0;
    expected_cleanup = 1;
    infinite_input = 0;
    serial_log[0] = 0;
    vga_calls = 0;
    expected_argc = 0;
    expected_argv = NULL;
    cleanup_exercises = 1;
}

static void rejected(RavenDoomEntry e, int argc, char **argv) {
    unsigned calls = alloc_calls;
    RavenDoomResult r = raven_doom_run(e, never_cleanup, argc, argv);
    CHECK(r.kind == RAVEN_DOOM_UNAVAILABLE && r.status == EINVAL);
    CHECK(!raven_doom_active() && !raven_doom_allocation_count());
    CHECK(!entry_calls && !cleanup_calls && !reset_calls && !unmount_calls);
    CHECK(!keyboard_calls && !serial_calls && alloc_calls == calls);
}

static void completed(RavenDoomResult r, RavenDoomResultKind kind, int status) {
    CHECK(r.kind == kind && r.status == status);
    CHECK(entry_calls == 1 && cleanup_calls == (unsigned)expected_cleanup);
    CHECK(reset_calls == 1 && unmount_calls == 1);
    CHECK(!streams_live && !raven_doom_active());
    CHECK(!raven_doom_allocation_count());
    CHECK(host_live == 1 && unrelated[0] == 0x73 && unrelated[16] == 0x73);
    CHECK(!keyboard_pending && !serial_pending);
    CHECK(keyboard_calls <= 2u * KEYBUF_CAP && serial_calls <= 2u * KEYBUF_CAP);
    KeyEvent event;
    char byte;
    CHECK(!keyboard_pop(&event) && !serial_read_char(&byte));
    CHECK(vga_calls == 0);
}

static void bounded_entry(int argc, char **argv) {
    (void)argc; (void)argv;
    entry_started = 1;
    streams_live = 1;
    CHECK(keyboard_calls == KEYBUF_CAP && serial_calls == KEYBUF_CAP);
}

static void partial_entry(int argc, char **argv) {
    (void)argc; (void)argv;
    CHECK(entry_calls++ == 0);
    entry_started = 1;
    streams_live = 1;
    CHECK(raven_doom_active() && raven_doom_allocation_count() == 0);
    CHECK(!keyboard_pending && !serial_pending);
    if (action == 1) raven_doom_exit(exit_status);
    if (action == 2) raven_doom_abort("before allocation");
}

static void partial_cleanup(void) {
    CHECK(cleanup_calls++ == 0);
    CHECK(raven_doom_active() && streams_live);
    CHECK(raven_doom_allocation_count() == 0);
    CHECK(reset_calls == 0 && unmount_calls == 0);
    /* Cleanup after a failure before the first allocation is still a normal
     * live allocation context. Leave it for the normal automatic reclamation. */
    session_memory = raven_doom_malloc(33);
    CHECK(session_memory != NULL);
    memset(session_memory, 0x5a, 33);
}

static unsigned reason_length, reason_on_stack;
static char *reason_source;
static char expected_reason[160];
static void snapshot_entry(int argc, char **argv) {
    (void)argc; (void)argv;
    char stack_reason[384];
    CHECK(entry_calls++ == 0 && raven_doom_abort_reason()[0] == 0);
    entry_started = streams_live = 1;
    session_memory = raven_doom_malloc(33);
    CHECK(session_memory != NULL);
    memset(session_memory, 0x5a, 33);
    char *source = reason_on_stack ? stack_reason : raven_doom_malloc(reason_length + 1);
    CHECK(source != NULL && reason_length < sizeof stack_reason);
    for (unsigned i = 0; i < reason_length; i++) source[i] = (char)('a' + i % 26);
    source[reason_length] = 0;
    reason_source = reason_on_stack ? NULL : source;
    raven_doom_abort(source);
}
static void snapshot_cleanup(void) {
    volatile char scratch[1024];
    for (unsigned i = 0; i < sizeof scratch; i++) scratch[i] = 'z';
    CHECK(scratch[sizeof scratch - 1] == 'z');
    CHECK(cleanup_calls++ == 0 && raven_doom_active());
    CHECK(strcmp(raven_doom_abort_reason(), expected_reason) == 0);
    check_nested_busy();
    if (reason_source) memset(reason_source, 'z', reason_length + 1);
}

static void fatal_cleanup(void) { raven_doom_exit(0); }
static void fatal_abort_cleanup(void) { raven_doom_abort("invalid cleanup"); }
static void child_entry(int argc, char **argv) { (void)argc; (void)argv; }
static void programming_error(unsigned which) {
    pid_t child = fork();
    CHECK(child >= 0);
    if (child == 0) {
        /* No runtime recovery is permitted for invalid boundary use. */
        if (which == 0) raven_doom_exit(0);
        if (which == 1) raven_doom_abort("outside session");
        (void)raven_doom_run(child_entry,
            which == 2 ? fatal_cleanup : fatal_abort_cleanup, 0, NULL);
        _exit(99);
    }
    int status;
    CHECK(waitpid(child, &status, 0) == child);
    CHECK(WIFSIGNALED(status) && WTERMSIG(status) == SIGILL);
    CHECK(!raven_doom_active());
}

int main(void) {
    inaccessible = mmap(NULL, 4096, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    CHECK(inaccessible != MAP_FAILED);
    unrelated = kmalloc(17);
    memset(unrelated, 0x73, 17);
    CHECK(raven_doom_malloc(16) == NULL && errno == EINVAL);
    CHECK(raven_doom_calloc(1, 16) == NULL && errno == EINVAL);
    CHECK(raven_doom_realloc(NULL, 16) == NULL && errno == EINVAL);
    raven_doom_free(inaccessible);
    CHECK(errno == EINVAL);
    CHECK(raven_doom_realloc(inaccessible, 2) == NULL && errno == EINVAL);

    prepare();
    char *bad[] = {NULL};
    rejected(NULL, 0, NULL);
    rejected(never_entry, -1, NULL);
    rejected(never_entry, 1, NULL);
    rejected(never_entry, 1, bad);
    interrupts_enabled = 0;
    rejected(never_entry, 0, NULL);
    interrupts_enabled = 1;

    action = 0;
    completed(raven_doom_run(entry, cleanup, 0, NULL), RAVEN_DOOM_RETURNED, 0);
    CHECK(serial_log[0] == 0);
    const int statuses[] = {0, 1, 12345, -1, -773, INT_MIN, INT_MAX};
    for (size_t i = 0; i < sizeof statuses / sizeof *statuses; i++) {
        prepare(); action = 1; exit_status = statuses[i];
        char *argv[] = {"doom", "-iwad", "/wad/freedoom1.wad", NULL};
        expected_argc = 3; expected_argv = argv;
        completed(raven_doom_run(entry, cleanup, 3, argv), RAVEN_DOOM_EXITED, exit_status);
        CHECK(serial_log[0] == 0);
    }
    prepare(); action = 2;
    completed(raven_doom_run(entry, cleanup, 0, NULL), RAVEN_DOOM_ABORTED, 1);
    CHECK(strcmp(serial_log, "Doom session error: partial graphics initialization failed\n") == 0);
    CHECK(strcmp(raven_doom_abort_reason(), "partial graphics initialization failed") == 0);
    prepare(); action = 3;
    completed(raven_doom_run(entry, cleanup, 0, NULL), RAVEN_DOOM_ABORTED, 1);
    CHECK(strcmp(serial_log, "Doom session error: abort\n") == 0);
    CHECK(strcmp(raven_doom_abort_reason(), "abort") == 0);
    prepare(); action = 0; expected_cleanup = 0;
    completed(raven_doom_run(entry, NULL, 0, NULL), RAVEN_DOOM_RETURNED, 0);
    CHECK(raven_doom_abort_reason()[0] == 0);

    const unsigned reason_lengths[] = {0, 1, 155, 156, 158, 159, 160, 161, 319};
    for (reason_on_stack = 0; reason_on_stack < 2; reason_on_stack++) {
        for (unsigned n = 0; n < sizeof reason_lengths / sizeof *reason_lengths; n++) {
            prepare();
            reason_length = reason_lengths[n];
            unsigned saved = reason_length < 159 ? reason_length : 159;
            for (unsigned i = 0; i < saved; i++) expected_reason[i] = (char)('a' + i % 26);
            expected_reason[saved] = 0;
            if (!reason_length) strcpy(expected_reason, "abort");
            if (reason_length > 159) memcpy(expected_reason + 156, "...", 3);
            completed(raven_doom_run(snapshot_entry, snapshot_cleanup, 0, NULL), RAVEN_DOOM_ABORTED, 1);
            CHECK(strcmp(raven_doom_abort_reason(), expected_reason) == 0);
            CHECK(strlen(raven_doom_abort_reason()) <= 159);
            /* Refusing a new entry must not erase the retained error. */
            RavenDoomResult refused = raven_doom_run(NULL, NULL, 0, NULL);
            CHECK(refused.kind == RAVEN_DOOM_UNAVAILABLE);
            CHECK(strcmp(raven_doom_abort_reason(), expected_reason) == 0);
        }
    }

    for (action = 0; action < 3; action++) {
        prepare(); exit_status = -81;
        RavenDoomResultKind kind = action == 0 ? RAVEN_DOOM_RETURNED :
            action == 1 ? RAVEN_DOOM_EXITED : RAVEN_DOOM_ABORTED;
        completed(raven_doom_run(partial_entry, partial_cleanup, 0, NULL), kind,
                  action == 1 ? exit_status : action == 2 ? 1 : 0);
    }

    /* Continuously arriving IRQ/serial input cannot spin forever at either
     * boundary. This test intentionally models an inexhaustible producer. */
    prepare(); infinite_input = 1; expected_cleanup = 0;
    RavenDoomResult r = raven_doom_run(bounded_entry, NULL, 0, NULL);
    CHECK(r.kind == RAVEN_DOOM_RETURNED);
    CHECK(keyboard_calls == 2u * KEYBUF_CAP && serial_calls == 2u * KEYBUF_CAP);
    CHECK(reset_calls == 1 && unmount_calls == 1 && !raven_doom_active());
    infinite_input = 0;

    for (unsigned i = 0; i < 64; i++) {
        prepare(); action = (int)(i % 4); exit_status = -(int)i;
        RavenDoomResultKind kind = action == 0 ? RAVEN_DOOM_RETURNED :
            action == 1 ? RAVEN_DOOM_EXITED : RAVEN_DOOM_ABORTED;
        completed(raven_doom_run(entry, cleanup, 0, NULL), kind,
                  action == 1 ? exit_status : action >= 2 ? 1 : 0);
    }
    prepare();
    for (unsigned i = 0; i < 4; i++) programming_error(i);
    CHECK(raven_doom_malloc(1) == NULL && errno == EINVAL);
    kfree(unrelated);
    CHECK(host_live == 0);
    CHECK(munmap(inaccessible, 4096) == 0);
    printf("runtime-test: %u checks passed (ASan/UBSan host boundary)\n", checks);
    return 0;
}

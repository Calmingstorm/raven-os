#ifndef RAVEN_DOOM_RUNTIME_H
#define RAVEN_DOOM_RUNTIME_H

#include <stddef.h>

typedef void (*RavenDoomEntry)(int argc, char **argv);
typedef void (*RavenDoomCleanup)(void);
typedef enum {
    RAVEN_DOOM_RETURNED = 0, RAVEN_DOOM_EXITED, RAVEN_DOOM_ABORTED,
    RAVEN_DOOM_UNAVAILABLE, RAVEN_DOOM_BUSY
} RavenDoomResultKind;
typedef struct { RavenDoomResultKind kind; int status; } RavenDoomResult;

/* Main loop, IF enabled. Non-nesting. CPU exceptions never use this boundary.
 * Cleanup runs once after entry return/exit/abort, while session memory and
 * streams remain live. It must be partial-init safe and must NOT exit/abort.
 * Cleanup restores graphics and port input, not engine I_AtExit callbacks. */
RavenDoomResult raven_doom_run(RavenDoomEntry entry, RavenDoomCleanup cleanup,
                              int argc, char **argv);
_Noreturn void raven_doom_exit(int status);
_Noreturn void raven_doom_abort(const char *reason);
/* Runtime-owned, NUL-terminated abort snapshot, at most 159 bytes including a
 * trailing "..." on truncation. Survives cleanup; cleared by the next accepted
 * run (not nested/preflight refusals). No console writes occur during abort. */
const char *raven_doom_abort_reason(void);
int raven_doom_active(void);

/* The libc allocation domain is deliberately separate from kernel kmalloc.
 * Outside a live session allocation fails. free/realloc reject non-session
 * pointers without reading memory behind them. Payloads remain 16B aligned. */
void *raven_doom_malloc(size_t size);
void *raven_doom_calloc(size_t count, size_t size);
void *raven_doom_realloc(void *ptr, size_t size);
void raven_doom_free(void *ptr);
size_t raven_doom_allocation_count(void);

void doom_command(void);
/* Supplied by the separately reviewed engine port, weakly linked until then. */
void raven_doom_port_main(int argc, char **argv);
void raven_doom_port_cleanup(void);

#endif

/* Host-only bridge, compiled WITHOUT custom headers/symbol renames. */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <setjmp.h>
#include <string.h>
static jmp_buf boundary;
static int boundary_kind, boundary_status;
int host_failures;
int host_alloc_fail;
void host_check(int condition, const char *expression, int line) {
    if (!condition) {
        fprintf(stderr, "libc:%d: %s\n", line, expression);
        ++host_failures;
    }
}
int host_vsnprintf(char *dst, size_t n, const char *fmt, va_list args) {
    return vsnprintf(dst, n, fmt, args);
}
void *raven_doom_malloc(size_t n) { return host_alloc_fail ? NULL : malloc(n); }
void *raven_doom_calloc(size_t n, size_t size) { return host_alloc_fail ? NULL : calloc(n, size); }
void *raven_doom_realloc(void *p, size_t n) { return host_alloc_fail ? NULL : realloc(p, n); }
void raven_doom_free(void *p) { free(p); }
_Noreturn void raven_doom_exit(int status) {
    boundary_kind = 1; boundary_status = status; longjmp(boundary, 1);
}
_Noreturn void raven_doom_abort(const char *reason) {
    if (!reason || !*reason) abort();
    boundary_kind = 2; longjmp(boundary, 1);
}
int host_boundary(void (*fn)(void), int kind, int status) {
    boundary_kind = boundary_status = 0;
    if (!setjmp(boundary)) fn();
    return boundary_kind == kind && (kind != 1 || boundary_status == status);
}
int host_result(void) {
    if (host_failures) fprintf(stderr, "libc: %d failures\n", host_failures);
    else puts("libc: native ASan/UBSan tests passed");
    return host_failures != 0;
}

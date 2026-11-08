#include <errno.h>
#include <stdio.h>
#include <sys/stat.h>
#include "raven_doom_runtime.h"

int remove(const char *path) { (void)path; errno = EROFS; return -1; }
int rename(const char *old_path, const char *new_path) {
    (void)old_path; (void)new_path; errno = EROFS; return -1;
}
int mkdir(const char *path, mode_t mode) {
    (void)path; (void)mode; errno = EROFS; return -1;
}
_Noreturn void raven_doom_assert_fail(const char *expression,
                                      const char *file, int line) {
    /* No allocation, no graphics writes. Formatting failure still aborts. */
    fprintf(stderr, "assertion failed: %s (%s:%d)\n", expression, file, line);
    raven_doom_abort("assertion failed");
}

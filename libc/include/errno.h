#ifndef RAVEN_ERRNO_H
#define RAVEN_ERRNO_H
/* Single main-loop context. Never access from interrupts. */
extern int errno;
#define ENOENT 2
#define EIO 5
#define EBADF 9
#define ENOMEM 12
#define EISDIR 21
#define EINVAL 22
#define EMFILE 24
#define EROFS 30
#define ERANGE 34
#define ENOSYS 38
#define EOVERFLOW 75
#define ETIMEDOUT 110
#endif

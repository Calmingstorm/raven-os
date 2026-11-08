#ifndef RAVEN_INTTYPES_H
#define RAVEN_INTTYPES_H
#include <stdint.h>
#define PRId8 "d"
#define PRIi8 "i"
#define PRIu8 "u"
#define PRIx8 "x"
#define PRIX8 "X"
#define PRId16 "d"
#define PRIi16 "i"
#define PRIu16 "u"
#define PRIx16 "x"
#define PRIX16 "X"
#define PRId32 "d"
#define PRIi32 "i"
#define PRIu32 "u"
#define PRIo32 "o"
#define PRIx32 "x"
#define PRIX32 "X"
#if __SIZEOF_LONG__ == 8
#define PRId64 "ld"
#define PRIi64 "li"
#define PRIu64 "lu"
#define PRIo64 "lo"
#define PRIx64 "lx"
#define PRIX64 "lX"
#else
#define PRId64 "lld"
#define PRIi64 "lli"
#define PRIu64 "llu"
#define PRIo64 "llo"
#define PRIx64 "llx"
#define PRIX64 "llX"
#endif
#if __SIZEOF_POINTER__ == __SIZEOF_LONG__
#define PRIdPTR "ld"
#define PRIuPTR "lu"
#define PRIxPTR "lx"
#else
#define PRIdPTR "d"
#define PRIuPTR "u"
#define PRIxPTR "x"
#endif
#endif

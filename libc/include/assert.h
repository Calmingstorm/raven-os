/* Deliberately no include guard: assert follows the current NDEBUG. */
#undef assert
_Noreturn void raven_doom_assert_fail(const char *expression,
                                      const char *file, int line);
#ifdef NDEBUG
#define assert(expr) ((void)0)
#else
#define assert(expr) ((expr) ? (void)0 : raven_doom_assert_fail(#expr, __FILE__, __LINE__))
#endif

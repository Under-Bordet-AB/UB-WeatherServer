/* sleep_ms.c - Portable millisecond delay for POSIX */
#include <errno.h>
#include <time.h>

/**
 * Sleep for the given number of milliseconds.
 *
 * @param ms  Milliseconds to sleep. 0 <= ms <= INT_MAX/1000.
 *            The function may return earlier if interrupted by a signal.
 */
static inline void sleep_ms(unsigned int ms) {
    struct timespec req = {.tv_sec = ms / 1000, .tv_nsec = (ms % 1000) * 1000000L};

    /* Keep sleeping until the entire interval has elapsed. */
    while (nanosleep(&req, &req) == -1 && errno == EINTR)
        ; // interrupted by signal; resume with remaining time in `req`
}
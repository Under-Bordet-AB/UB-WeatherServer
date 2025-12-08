#ifndef MISC_UTILS_H
#define MISC_UTILS_H

#include <time.h>
#include <stdint.h>

/*
 * Sleep for approximately 1 millisecond.
 * Implemented as static inline so no separate .c file is required.
 */
static inline void misc_sleep_1ms(void)
{
	struct timespec ts;
	ts.tv_sec = 0;
	ts.tv_nsec = 1000 * 1000; /* 1 ms = 1,000,000 ns */
	nanosleep(&ts, NULL);
}

#endif /* MISC_UTILS_H */


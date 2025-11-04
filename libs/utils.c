#define _POSIX_C_SOURCE 199309L
#include "utils.h"
#include <time.h>

uint64_t SystemMonotonicMS(void)
{
    long ms;
    time_t s;

    struct timespec spec;
    clock_gettime(CLOCK_MONOTONIC, &spec);
    s = spec.tv_sec;
    ms = (spec.tv_nsec / 1000000);

    uint64_t result = (uint64_t)s;
    result *= 1000ULL;
    result += (uint64_t)ms;

    return result;
}
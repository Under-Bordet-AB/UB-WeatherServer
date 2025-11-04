/* utils.h - small utility functions used across the project */
#ifndef UB_WEATHERSERVER_UTILS_H
#define UB_WEATHERSERVER_UTILS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Return monotonic system time in milliseconds. */
uint64_t SystemMonotonicMS(void);

#ifdef __cplusplus
}
#endif

#endif /* UB_WEATHERSERVER_UTILS_H */

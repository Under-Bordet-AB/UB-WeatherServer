#ifndef UB_LOGGER_H
#define UB_LOGGER_H

#include <stdarg.h>

typedef struct logger {
    void (*info)(logger* l, const char* fmt, ...);
    void (*warn)(logger* l, const char* fmt, ...);
    void (*error)(logger* l, const char* fmt, ...);
    void (*debug)(logger* l, const char* fmt, ...);
    void (*destroy)(logger* l);
    void* impl_data;
} logger;

// Create a simple stdout/stderr logger. The returned `logger` must be
// freed with `logger->destroy(logger)`.
logger* ub_simple_logger_create(void);

#endif // UB_LOGGER_H

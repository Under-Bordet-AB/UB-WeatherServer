#ifndef UB_FILTER_H
#define UB_FILTER_H

#include "../core/context.h"

/**
 * @brief Filter Status
 * Indicates what the pipeline should do next.
 */
typedef enum {
    FILTER_CONTINUE, // Proceed to next filter
    FILTER_STOP,     // Stop processing (response generated or error)
    FILTER_WAIT      // Async operation started, pause pipeline
} filter_status;

/**
 * @brief Filter Interface
 * A single step in request processing (e.g. Auth, RateLimit, BusinessLogic)
 */
typedef struct filter {
    const char* name;

    /**
     * @brief Process the request
     * @param ctx Request context
     * @param data Input data (e.g. HTTP Request)
     * @param out_data Output data (e.g. HTTP Response)
     */
    filter_status (*process)(struct filter* self, ub_context* ctx, void* data, void** out_data);

    void (*destroy)(struct filter* self);
} filter;

#endif // UB_FILTER_H

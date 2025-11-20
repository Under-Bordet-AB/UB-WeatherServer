#ifndef UB_CONTEXT_H
#define UB_CONTEXT_H

#include "../observability/metrics.h"
#include <stddef.h>

/**
 * @brief Request Context
 * Holds data that needs to persist or be accessible throughout the lifecycle
 * of a request/connection.
 */
typedef struct ub_context {
    // Unique ID for tracing
    uint64_t request_id;

    // Logger handle (could be added later)
    // void* logger;

    // Metrics recorder for this specific context (e.g. tagged with client ID)
    metrics_recorder* metrics;

    // User data / Session data
    void* user_data;
} ub_context;

#endif // UB_CONTEXT_H

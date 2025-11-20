#ifndef UB_BACKEND_INTERFACE_H
#define UB_BACKEND_INTERFACE_H

#include "../core/context.h"

/**
 * @brief Backend Callback
 * Called when data is ready.
 */
typedef void (*backend_cb)(ub_context* ctx, void* result, void* user_data);

/**
 * @brief Backend Interface
 * Abstract source of data (Weather API, DB, Cache)
 */
typedef struct backend {
    const char* name;
    void* impl_data;

    /**
     * @brief Initialize backend
     */
    int (*init)(struct backend* self, void* config);

    /**
     * @brief Fetch data asynchronously
     * @param query The query object (e.g. city name)
     * @param cb Callback function
     * @param cb_arg User data for callback
     */
    int (*fetch)(struct backend* self, ub_context* ctx, const char* query, backend_cb cb, void* cb_arg);

    void (*destroy)(struct backend* self);
} backend;

#endif // UB_BACKEND_INTERFACE_H

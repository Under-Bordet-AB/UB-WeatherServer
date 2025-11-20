#ifndef UB_LISTENER_H
#define UB_LISTENER_H

#include "../processing/pipeline.h"
#include <stdint.h>

// Forward declarations
typedef struct ub_server ub_server;
typedef struct mj_scheduler mj_scheduler;

/**
 * @brief Listener Interface
 * Abstract base class for network listeners.
 */
typedef struct listener {
    // Context/Instance data
    void* impl_data;

    /**
     * @brief Initialize the listener (bind sockets, etc)
     */
    int (*init)(struct listener* self, const char* addr, const char* port);

    /**
     * @brief Start listening and register with scheduler
     */
    int (*start)(struct listener* self, mj_scheduler* scheduler);

    /**
     * @brief Stop listening
     */
    int (*stop)(struct listener* self);

    /**
     * @brief Destroy and free resources
     */
    void (*destroy)(struct listener* self);

} listener;

/**
 * @brief Factory for a standard TCP HTTP Listener
 */
listener* ub_create_http_listener(void);

// Set a default pipeline that new connections will inherit. Passing NULL
// removes any default.
void ub_http_listener_set_pipeline(listener* l, pipeline* pipeline);

#endif // UB_LISTENER_H

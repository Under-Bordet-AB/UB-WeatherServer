#ifndef UB_SERVER_H
#define UB_SERVER_H

/*
 * design_sketch/include/core/server.h
 *
 * Composition root: ub_server
 * - Uses an external scheduler (mj_scheduler)
 * - Holds references to listeners and backend registry
 * - Provides lifecycle and observability attachment functions
 */

#include "../backend/backend_interface.h"
#include "../network/listener.h"
#include "../observability/logger.h"
#include "../observability/metrics.h"
#include "context.h"
#include <stdint.h>
#include <stdlib.h>

// Forward declaration of the external scheduler
typedef struct mj_scheduler mj_scheduler;

/**
 * Server configuration for the demo
 */
typedef struct ub_server_config {
    const char* bind_address; /* NULL => use default */
    const char* port;         /* NULL => use default */
    int max_connections;      /* soft limit for demo purposes */
} ub_server_config;

/**
 * Server composition root
 * Note: The `scheduler` is external and not owned by ub_server. The
 * application must create/destroy the scheduler lifetime.
 */
typedef struct ub_server {
    ub_server_config config;
    mj_scheduler* scheduler; /* external, owned by caller */

    /* linked list of registered listeners */
    struct listener_node* listeners;
    /* linked list of registered backends */
    struct backend_registry* backends;

    /* runtime flags */
    int is_running;

    /* Observability - optional handlers configured by caller */
    metrics_recorder* metrics; /* optional, owned by caller */
    logger* logger;            /* optional, owned by caller */
} ub_server;

/* Create a server instance bound to an existing scheduler
 * Returns NULL on allocation or parameter error. */
ub_server* ub_server_create_with_scheduler(const ub_server_config* config, mj_scheduler* scheduler);

/* Set observability hooks, ownership stays with caller */
void ub_server_set_metrics(ub_server* server, metrics_recorder* metrics);
void ub_server_set_logger(ub_server* server, logger* logger);

/* Register a listener (server takes ownership) */
int ub_server_add_listener(ub_server* server, listener* l);

/* Register a backend under a logical name (server keeps reference) */
int ub_server_register_backend(ub_server* server, const char* name, backend* backend);

/* Start/stop all listeners registered with the server. These functions
 * register listener tasks on the external scheduler but do not run or
 * destroy the scheduler itself.
 */
int ub_server_start_listeners(ub_server* server);
int ub_server_stop_listeners(ub_server* server);

/* Convenience wrapper - alias for start_listeners */
static inline int ub_server_run(ub_server* server) {
    return ub_server_start_listeners(server);
}

/* Stop server and release resources. Scheduler lifecycle is not managed
 * by ub_server; it remains the caller's responsibility.
 */
void ub_server_stop(ub_server* server);
void ub_server_destroy(ub_server* server);

#endif // UB_SERVER_H

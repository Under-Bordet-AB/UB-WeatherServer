#include "../../include/core/server.h"
#include <stdio.h>
#include <stdlib.h>

// Placeholder for the actual scheduler implementation
// In the real project, we would include "majjen.h"
// But since this is a design sketch, we assume the interface exists.
// We need to define the struct if we want to compile this sketch independently,
// or include the real header if we are integrating.
// For the sketch, let's assume we link against the real majjen.c or a mock.
// I will include the real header path relative to this file.
#include "../../../src/libs/majjen.h"
#include "../../include/observability/logger.h"
#include "../../include/observability/metrics.h"

struct listener_node {
    listener* listener;
    struct listener_node* next;
};

struct backend_registry {
    char* name;
    backend* backend;
    struct backend_registry* next;
};

ub_server* ub_server_create_with_scheduler(const ub_server_config* config, mj_scheduler* scheduler) {
    if (!scheduler) {
        // Server requires an external scheduler; do not create one internally.
        return NULL;
    }

    ub_server* server = malloc(sizeof(ub_server));
    if (!server)
        return NULL;

    server->config = *config;
    server->listeners = NULL;
    server->backends = NULL;
    server->is_running = 0;

    server->scheduler = scheduler;
    server->metrics = NULL;
    server->logger = NULL;

    return server;
}

int ub_server_add_listener(ub_server* server, listener* listener) {
    struct listener_node* node = malloc(sizeof(struct listener_node));
    if (!node)
        return -1;

    node->listener = listener;
    node->next = server->listeners;
    server->listeners = node;

    // Initialize the listener
    if (listener->init) {
        listener->init(listener, server->config.bind_address, server->config.port);
    }

    return 0;
}

int ub_server_register_backend(ub_server* server, const char* name, backend* backend) {
    // Simple linked list registry
    struct backend_registry* node = malloc(sizeof(struct backend_registry));
    // ... (strdup name, etc)
    node->backend = backend;
    node->next = server->backends;
    server->backends = node;
    return 0;
}

void ub_server_set_metrics(ub_server* server, metrics_recorder* metrics) {
    if (!server)
        return;
    server->metrics = metrics;
}

void ub_server_set_logger(ub_server* server, logger* logger) {
    if (!server)
        return;
    server->logger = logger;
}

int ub_server_start_listeners(ub_server* server) {
    if (!server)
        return -1;
    server->is_running = 1;

    // Start all listeners (register their tasks on server->scheduler)
    struct listener_node* curr = server->listeners;
    while (curr) {
        if (curr->listener->start) {
            int rc = curr->listener->start(curr->listener, server->scheduler);
            if (rc != 0) {
                if (server->logger)
                    server->logger->warn(server->logger, "listener start returned %d", rc);
            } else {
                if (server->logger)
                    server->logger->info(server->logger, "listener started");
                if (server->metrics && g_metrics && g_metrics->inc_counter) {
                    g_metrics->inc_counter(server->metrics, "listeners_started", NULL);
                }
            }
        }
        curr = curr->next;
    }

    // The scheduler is external and is the program's master. The server
    // registers listener tasks on the provided scheduler but never runs
    // or destroys it.
    return 0;
}

int ub_server_stop_listeners(ub_server* server) {
    if (!server)
        return -1;

    struct listener_node* curr = server->listeners;
    while (curr) {
        if (curr->listener->stop) {
            int rc = curr->listener->stop(curr->listener);
            if (rc != 0) {
                if (server->logger)
                    server->logger->warn(server->logger, "listener stop returned %d", rc);
            } else {
                if (server->logger)
                    server->logger->info(server->logger, "listener stopped");
                if (server->metrics && g_metrics && g_metrics->inc_counter) {
                    g_metrics->inc_counter(server->metrics, "listeners_stopped", NULL);
                }
            }
        }
        curr = curr->next;
    }

    server->is_running = 0;
    return 0;
}

void ub_server_destroy(ub_server* server) {
    if (!server)
        return;

    // Stop and destroy listeners
    struct listener_node* curr = server->listeners;
    while (curr) {
        struct listener_node* next = curr->next;
        if (curr->listener->stop)
            curr->listener->stop(curr->listener);
        if (curr->listener->destroy)
            curr->listener->destroy(curr->listener);
        free(curr);
        curr = next;
    }

    // Server must not destroy the external scheduler; scheduler lifecycle
    // is the responsibility of the application that created it.

    // Do not free metrics/logger; they are owned by caller. Just clear refs.
    server->metrics = NULL;
    server->logger = NULL;

    free(server);
}

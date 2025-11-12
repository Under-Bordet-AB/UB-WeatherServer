#pragma once

#include "majjen.h"
#include <netinet/in.h>
#include <time.h>

// Server error codes
typedef enum {
    W_SERVER_ERROR_NONE = 0,
    W_SERVER_ERROR_SOCKET_CREATE,
    W_SERVER_ERROR_SOCKET_BIND,
    W_SERVER_ERROR_SOCKET_LISTEN,
    W_SERVER_ERROR_GETADDRINFO,
    W_SERVER_ERROR_MEMORY_ALLOCATION,
    W_SERVER_ERROR_SCHEDULER_FULL,
    W_SERVER_ERROR_INVALID_PORT,
    W_SERVER_ERROR_INVALID_ADDRESS,
    W_SERVER_ERROR_INTERNAL
} w_server_error;

// Server configuration
typedef struct w_server_config {
    const char* address; // NULL or "0.0.0.0" for all interfaces, "127.0.0.1" for localhost only
    const char* port;    // Port number as string, e.g., "8080"
    int backlog;         // Listen backlog, 0 for default (128)
} w_server_config;

/* ----------------------------------------------------------------------
 *      Server, lives for entire program, does no work. Just holds state
 * ---------------------------------------------------------------------- */
typedef struct w_server {
    // Sheduler for all state machines
    mj_scheduler* scheduler;

    // Network
    int listen_fd;
    char address[46]; // IPv6 max length (INET6_ADDRSTRLEN)
    char port[6];     // Port string (max 65535)

    // Metrics
    size_t active_count;

    // Last error
    w_server_error last_error;
} w_server;

int w_server_create(mj_scheduler* scheduler, w_server* server, const w_server_config* config);
void w_server_destroy(w_server* server);
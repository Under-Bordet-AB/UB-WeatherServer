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

/* Forward declaration of the opaque server type.  The definition lives in
 * w_server.c; users only ever manipulate pointers to it. */
typedef struct w_server w_server;

// Server configuration
typedef struct w_server_config {
    const char* address; // NULL or "0.0.0.0" for all interfaces, "127.0.0.1" for localhost only
    const char* port;    // Port number as string, e.g., "8080"
    int backlog;         // Listen backlog, 0 for default (128)
} w_server_config;

int w_server_create(mj_scheduler* scheduler, w_server* server, const w_server_config* config);
void w_server_destroy(w_server* server);
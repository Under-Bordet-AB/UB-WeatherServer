#pragma once

#include "majjen.h"
#include <netinet/in.h>
#include <time.h>

typedef enum {
    W_SERVER_ERROR_NONE = 0,
    W_SERVER_ERROR_SOCKET_CREATE,
    W_SERVER_ERROR_SOCKET_BIND,
    W_SERVER_ERROR_SOCKET_LISTEN,
    W_SERVER_ERROR_GETADDRINFO,
    W_SERVER_ERROR_MEMORY_ALLOCATION,
    W_SERVER_ERROR_NO_CONFIG,
    W_SERVER_ERROR_INVALID_CONFIG,
    W_SERVER_ERROR_SCHEDULER_FULL,
    W_SERVER_ERROR_INVALID_PORT,
    W_SERVER_ERROR_INVALID_ADDRESS,
    W_SERVER_ERROR_INTERNAL,
    W_SERVER_ERROR
} w_server_error;

typedef struct w_server_config {
    const char* address; // NULL or "0.0.0.0" for all interfaces, "127.0.0.1" for localhost only
    const char* port;    // Port number as string, e.g., "8080"
    int backlog;         // Listen backlog, 0 for default (128)
} w_server_config;

typedef struct w_server {
    mj_task* w_server_listen_tasks;

    // Network
    int listen_fd;
    char address[46]; // IPv6 max length (INET6_ADDRSTRLEN)
    char port[6];     // Port string (max 65535)

    // Metrics
    size_t active_count;

    // Last error
    w_server_error last_error;
} w_server;

w_server* w_server_create(w_server_config* config);
void w_server_destroy(w_server* server);

///////////////////////////////////////////////////////////////////////////////
// Från AI nedanför
/*
typedef enum {
    CLIENT_STATE_READ_REQUEST,    // Reading HTTP request
    CLIENT_STATE_PARSE_REQUEST,   // Parsing what we read
    CLIENT_STATE_PROCESS,         // Business logic (look up weather, etc.)
    CLIENT_STATE_WRITE_RESPONSE,  // Writing HTTP response
    CLIENT_STATE_DONE            // Cleanup and remove
} client_state_t;

typedef struct {
    int fd;
    client_state_t state;

    // Buffers for reading/writing
    char read_buffer[4096];
    size_t read_pos;

    char write_buffer[4096];
    size_t write_pos;
    size_t write_total;

    // Parsed request data
    char method[16];
    char path[256];

    // Response data
    char* response_body;
    size_t response_len;
} w_client;
 */
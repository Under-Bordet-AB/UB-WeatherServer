#pragma once

#include "majjen.h"
#include "w_server.h"
#include <netinet/in.h>
#include <time.h>

#define W_CLIENT_READ_BUFFER_SIZE 8192
#define W_CLIENT_MAX_REQUEST_SIZE (1 * 1024 * 1024) // 1MB

// Client error codes
typedef enum {
    W_CLIENT_ERROR_NONE = 0,
    W_CLIENT_ERROR_INIT_SOCKET,
    W_CLIENT_ERROR_INIT_MEMORY,
    W_CLIENT_ERROR_READ,
    W_CLIENT_ERROR_WRITE,
    W_CLIENT_ERROR_SOCKET_CLOSED,
    W_CLIENT_ERROR_REQUEST_TOO_LARGE,
    W_CLIENT_ERROR_MALFORMED_REQUEST,
    W_CLIENT_ERROR_INVALID_HTTP_METHOD,
    W_CLIENT_ERROR_INVALID_HTTP_VERSION,
    W_CLIENT_ERROR_UNSUPPORTED_CONTENT_TYPE,
    W_CLIENT_ERROR_TIMEOUT,
    W_CLIENT_ERROR_MEMORY_ALLOCATION,
    W_CLIENT_ERROR_INTERNAL
} w_client_error;

// Per-connection state machine states
typedef enum {
    W_CLIENT_READING,
    W_CLIENT_PARSING,
    W_CLIENT_PROCESSING,
    W_CLIENT_SENDING,
    W_CLIENT_DONE
} w_client_state;

// CLIENT context, one per active client
typedef struct w_client {
    // State machine management
    w_client_state state;
    // Network
    int fd;
    size_t client_number; // Sequential client number
    // Reference to server (for metrics)
    w_server* server;
    // HTTP parsing
    char read_buffer[W_CLIENT_READ_BUFFER_SIZE];
    size_t bytes_read;
    // Request
    char* request_body;
    uint8_t* request_body_raw;
    size_t request_body_len;
    void* parsed_request;
    // Response (if these are not NULL we are ready to send)
    char* response_data;
    size_t response_len;
    size_t response_sent;
    // Metrics
    struct timespec connect_time;
    w_client_error error_code;
} w_client;

mj_task* w_client_create(int client_fd, w_server* server);

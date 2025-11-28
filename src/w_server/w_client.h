#pragma once

#include "global_defines.h"
#include "majjen.h"
#include "w_server.h"
#include <time.h>

// Client error codes
typedef enum {
    W_CLIENT_ERROR_NONE = 0,
    W_CLIENT_ERROR_READ,
    W_CLIENT_ERROR_TIMEOUT,
    W_CLIENT_ERROR_ROUTE_GEOFETCH, // TODO use this error enum
    W_CLIENT_ERROR_ROUTE_WEATHER,  // TODO use this error enum
    W_CLIENT_ERROR_ROUTE_SURPRISE,
    W_CLIENT_ERROR_REQUEST_TOO_LARGE,
    W_CLIENT_ERROR_MALFORMED_REQUEST,
    W_CLIENT_ERROR_INTERNAL,
    W_CLIENT_ERROR_SEND,
    W_CLIENT_ERROR_SEND_EPIPE,
    W_CLIENT_ERROR_SEND_ECONNRESET,
    W_CLIENT_ERROR_SEND_EFAULT
} w_client_error;

// Per-connection state machine states
typedef enum {
    W_CLIENT_READING,
    W_CLIENT_PARSING,
    W_CLIENT_PROCESSING,
    W_CLIENT_WAITING_TASK, // Waiting for external task (e.g., geocode_weather) to complete
    W_CLIENT_SENDING,
    W_CLIENT_DONE
} w_client_state;

// CLIENT context, one per active client
typedef struct w_client {
    int fd;
    w_client_state state;
    size_t client_number;
    w_server* server;

    char read_buffer[W_CLIENT_READ_BUFFER_SIZE];
    size_t bytes_read;

    // Request
    char* request_body;
    uint8_t* request_body_raw;
    size_t request_body_len;
    void* parsed_request; // This a http_request struct
    // This should be a list of multiple params and also not a separate field in the struct
    char req_location[W_CLIENT_REQ_LOCATION_MAX_SIZE];

    // Response
    char* response_body;
    size_t response_body_size;
    size_t response_len;
    size_t response_sent;

    // Metrics
    struct timespec connect_time;
    w_client_error error_code;
} w_client;

mj_task* w_client_create(int client_fd, w_server* server);

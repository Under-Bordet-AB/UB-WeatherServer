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
    mj_task* w_server_accept_clients_func;

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


///////////////////////////////////////////////////////////////////////////////
// Från AI nedanför

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

void w_client_task_func(mj_scheduler* scheduler, void* state) {
    w_client* client = (w_client*)state;
    
    switch (client->state) {
        case CLIENT_STATE_READ_REQUEST:
            // Try to read (non-blocking)
            ssize_t n = read(client->fd, 
                           client->read_buffer + client->read_pos,
                           sizeof(client->read_buffer) - client->read_pos - 1);
            
            if (n > 0) {
                client->read_pos += n;
                client->read_buffer[client->read_pos] = '\0';
                
                // Check if we have complete request (look for \r\n\r\n)
                if (strstr(client->read_buffer, "\r\n\r\n")) {
                    client->state = CLIENT_STATE_PARSE_REQUEST;
                }
            } else if (n == 0) {
                // Client closed connection
                client->state = CLIENT_STATE_DONE;
            } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
                // Real error
                client->state = CLIENT_STATE_DONE;
            }
            // If EAGAIN/EWOULDBLOCK, just return and try again next time
            break;
            
        case CLIENT_STATE_PARSE_REQUEST:
            // Parse HTTP request (extract method, path, etc.)
            // This is fast, so we can do it all at once
            parse_http_request(client);
            client->state = CLIENT_STATE_PROCESS;
            break;
            
        case CLIENT_STATE_PROCESS:
            // Handle the request (GET /weather?city=Stockholm)
            // Generate response
            handle_request(client);
            client->state = CLIENT_STATE_WRITE_RESPONSE;
            break;
            
        case CLIENT_STATE_WRITE_RESPONSE:
            // Try to write (non-blocking)
            ssize_t written = write(client->fd,
                                  client->write_buffer + client->write_pos,
                                  client->write_total - client->write_pos);
            
            if (written > 0) {
                client->write_pos += written;
                if (client->write_pos >= client->write_total) {
                    // Done writing!
                    client->state = CLIENT_STATE_DONE;
                }
            } else if (written < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                // Error
                client->state = CLIENT_STATE_DONE;
            }
            break;
            
        case CLIENT_STATE_DONE:
            // Cleanup
            close(client->fd);
            free(client->response_body);
            
            // Remove this task from scheduler
            mj_scheduler_task_remove(scheduler, w_client_task_func, client);
            free(client);
            break;
    }
}
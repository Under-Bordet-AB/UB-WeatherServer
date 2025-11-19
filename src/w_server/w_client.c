#include "w_client.h"
#include "http_parser.h"
#include "majjen.h"
#include "string.h"
#include "utils.h"
#include "w_server.h"
#include <errno.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

#define CLIENT_TIMEOUT_SEC 10

// ANSI color codes (48 colors ordered for maximum contrast and readability)
#define COLOR_RESET "\033[0m"
static const char* client_colors[] = {
    "\033[38;5;196m", // Red
    "\033[38;5;51m",  // Cyan
    "\033[38;5;226m", // Yellow
    "\033[38;5;21m",  // Blue
    "\033[38;5;46m",  // Green
    "\033[38;5;201m", // Magenta
    "\033[38;5;214m", // Orange
    "\033[38;5;87m",  // Sky Blue
    "\033[38;5;154m", // Light Green
    "\033[38;5;129m", // Purple
    "\033[38;5;220m", // Gold
    "\033[38;5;39m",  // Deep Blue
    "\033[38;5;160m", // Dark Red
    "\033[38;5;50m",  // Turquoise
    "\033[38;5;190m", // Yellow-Green
    "\033[38;5;93m",  // Purple-Blue
    "\033[38;5;202m", // Orange-Red
    "\033[38;5;45m",  // Bright Cyan
    "\033[38;5;118m", // Lime
    "\033[38;5;165m", // Magenta-Purple
    "\033[38;5;208m", // Dark Orange
    "\033[38;5;33m",  // Dodger Blue
    "\033[38;5;40m",  // Bright Green
    "\033[38;5;199m", // Hot Pink
    "\033[38;5;184m", // Yellow-Orange
    "\033[38;5;27m",  // Ocean Blue
    "\033[38;5;82m",  // Spring Green
    "\033[38;5;135m", // Violet
    "\033[38;5;166m", // Orange Brown
    "\033[38;5;75m",  // Steel Blue
    "\033[38;5;34m",  // Forest Green
    "\033[38;5;205m", // Pink
    "\033[38;5;178m", // Gold Orange
    "\033[38;5;63m",  // Medium Blue
    "\033[38;5;148m", // Olive Green
    "\033[38;5;170m", // Orchid
    "\033[38;5;172m", // Burnt Orange
    "\033[38;5;117m", // Light Blue
    "\033[38;5;76m",  // Chartreuse
    "\033[38;5;141m", // Lavender
    "\033[38;5;209m", // Peach
    "\033[38;5;69m",  // Cornflower Blue
    "\033[38;5;113m", // Yellow Green
    "\033[38;5;177m", // Plum
    "\033[38;5;215m", // Light Orange
    "\033[38;5;81m",  // Aqua
    "\033[38;5;156m", // Pale Green
    "\033[38;5;207m", // Light Magenta
};
#define NUM_COLORS (sizeof(client_colors) / sizeof(client_colors[0]))

// State machine for clients
void w_client_run(mj_scheduler* scheduler, void* ctx) {
    if (scheduler == NULL || ctx == NULL) {
        return;
    }

    w_client* client = (w_client*)ctx;
    const char* color = client_colors[client->client_number % NUM_COLORS];

    switch (client->state) {
    case W_CLIENT_READING:
        // Check for timeout directly (inline, no function call)
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        time_t elapsed_sec = now.tv_sec - client->connect_time.tv_sec;
        if (elapsed_sec > CLIENT_TIMEOUT_SEC ||
            (elapsed_sec == CLIENT_TIMEOUT_SEC && now.tv_nsec >= client->connect_time.tv_nsec)) {
            fprintf(stderr, "%sClient %4zu (active: %4zu, total: %4zu) Connection timeout (%ds)%s\n", color,
                    client->client_number, client->server->active_count, client->server->total_clients,
                    CLIENT_TIMEOUT_SEC, COLOR_RESET);
            client->error_code = W_CLIENT_ERROR_TIMEOUT;
            client->state = W_CLIENT_DONE;
            return;
        }

        // Try to read data from the client socket (already set as non blocking)
        ssize_t bytes = recv(client->fd, client->read_buffer + client->bytes_read,
                             sizeof(client->read_buffer) - client->bytes_read - 1, 0);

        if (bytes < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // Non-blocking socket, no data available yet
                return;
            }
            // Read error
            fprintf(stderr, "%sClient %4zu (active: %4zu, total: %4zu) Read error: %s%s\n", color,
                    client->client_number, client->server->active_count, client->server->total_clients, strerror(errno),
                    COLOR_RESET);
            client->error_code = W_CLIENT_ERROR_READ;
            client->state = W_CLIENT_DONE;
            return;
        }

        // Client closed connection (FIN received)
        if (bytes == 0) {
            fprintf(stderr, "%sClient %4zu (active: %4zu, total: %4zu) Connection closed by client%s\n", color,
                    client->client_number, client->server->active_count, client->server->total_clients, COLOR_RESET);
            client->state = W_CLIENT_DONE;
            return;
        }

        fprintf(stderr, "%sClient %4zu (active: %4zu, total: %4zu) Received %zd bytes (total: %zu)%s\n", color,
                client->client_number, client->server->active_count, client->server->total_clients, bytes,
                client->bytes_read + bytes, COLOR_RESET);

        // Update bytes read and null-terminate
        client->bytes_read += bytes;
        client->read_buffer[client->bytes_read] = '\0';

        // Check if we have a complete HTTP request (ends with \r\n\r\n).
        // We must wait for completed request since current parser does not support incremental parsing.
        // NOTE, this check is "message framing" so it does not belong in parsing
        if (strstr(client->read_buffer, "\r\n\r\n") != NULL) {
            client->state = W_CLIENT_PARSING;
        } else if (client->bytes_read >= sizeof(client->read_buffer) - 1) {
            // Buffer full but no complete request
            fprintf(stderr, "%sClient %4zu (active: %4zu, total: %4zu) Request too large (buffer full)%s\n", color,
                    client->client_number, client->server->active_count, client->server->total_clients, COLOR_RESET);
            client->error_code = W_CLIENT_ERROR_REQUEST_TOO_LARGE;
            client->state = W_CLIENT_DONE;
        }
        break;

    case W_CLIENT_PARSING:
        // Parse the complete HTTP request
        http_request* parsed = http_request_fromstring(client->read_buffer);

        if (!parsed || !parsed->valid) {
            // Parse error - send 400 Bad Request
            fprintf(stderr, "%sClient %4zu (active: %4zu, total: %4zu) Failed to parse HTTP request%s\n", color,
                    client->client_number, client->server->active_count, client->server->total_clients, COLOR_RESET);
            if (parsed)
                http_request_dispose(&parsed);
            client->error_code = W_CLIENT_ERROR_MALFORMED_REQUEST;
            client->state = W_CLIENT_DONE;
            return;
        }

        // Store parsed request in client context (cast to void* as per struct)
        client->parsed_request = parsed;

        // Log the parsed request
        fprintf(stderr, "%sClient %4zu (active: %4zu, total: %4zu) Request: %s %s HTTP/%d.%d%s\n", color,
                client->client_number, client->server->active_count, client->server->total_clients,
                request_method_tostring(parsed->method), parsed->url, parsed->protocol / 10, parsed->protocol % 10,
                COLOR_RESET);

        client->state = W_CLIENT_PROCESSING;
        break;

    case W_CLIENT_PROCESSING: {
        // Route request and generate response (backend not implemented yet)
        http_request* req = (http_request*)client->parsed_request;
        http_response* response = NULL;

        fprintf(stderr, "%sClient %4zu (active: %4zu, total: %4zu) Processing request...%s\n", color,
                client->client_number, client->server->active_count, client->server->total_clients, COLOR_RESET);

        // Simple routing based on method and URL
        if (req->method == REQUEST_METHOD_GET && strcmp(req->url, "/") == 0) {
            response = http_response_new(RESPONSE_CODE_OK, "Hello from weather server!");
        } else if (req->method == REQUEST_METHOD_POST && strcmp(req->url, "/data") == 0) {
            // POST not implemented yet (no body parsing)
            response = http_response_new(RESPONSE_CODE_NOT_IMPLEMENTED, "POST not implemented");
        } else {
            // 404 for all other routes
            response = http_response_new(RESPONSE_CODE_NOT_FOUND, "Not found");
        }

        // Add standard headers
        http_response_add_header(response, "Content-Type", "text/plain");
        http_response_add_header(response, "Connection", "close");

        // Serialize response to string
        const char* response_str = http_response_tostring(response);
        client->response_data = (char*)response_str; // Store for sending
        client->response_len = strlen(response_str);
        client->response_sent = 0; // Track how much we've sent

        fprintf(stderr, "%sClient %4zu (active: %4zu, total: %4zu) Response: %d %s (%zu bytes)%s\n", color,
                client->client_number, client->server->active_count, client->server->total_clients, response->code,
                response_code_tostring(response->code), client->response_len, COLOR_RESET);

        // Clean up response struct (string is already copied)
        http_response_dispose(&response);

        client->state = W_CLIENT_SENDING;
        break;
    }

    case W_CLIENT_SENDING:
        client->state = W_CLIENT_DONE;
        break;

    case W_CLIENT_DONE:
        mj_scheduler_task_remove_current(scheduler); /* invokes w_client_cleanup() */
        break;

    default: /* defensive path */
        fprintf(stderr, "%sClient %4zu (active: %4zu, total: %4zu) ERROR: Unknown state %d, forcing cleanup%s\n", color,
                client->client_number, client->server->active_count, client->server->total_clients, client->state,
                COLOR_RESET);
        client->state = W_CLIENT_DONE; /* force a cleanup cycle */
        break;
    }
}

// deallocate any internal reasources added to the context
void w_client_cleanup(mj_scheduler* scheduler, void* ctx) {
    w_client* client = (w_client*)ctx;

    // Decrement active client counter
    if (client->server) {
        client->server->active_count--;
    }

    // close socket
    if (client->fd >= 0) {
        shutdown(client->fd, SHUT_WR);
        close(client->fd);
        client->fd = -1;
    }

    // Free parsed HTTP request
    if (client->parsed_request) {
        http_request* req = (http_request*)client->parsed_request;
        http_request_dispose(&req);
        client->parsed_request = NULL;
    }

    // Free response data
    if (client->response_data) {
        free(client->response_data);
        client->response_data = NULL;
    }

    // Deallocate any buffers and resources here, the instances ctx gets deallocated by the scheduler.
}

// Create a client
mj_task* w_client_create(int client_fd, w_server* server) {
    if (client_fd < 0) {
        fprintf(stderr, " ERROR: [%s:%d %s]\n", __FILE__, __LINE__, __func__);
        return NULL;
    }
    if (server == NULL) {
        fprintf(stderr, " ERROR: [%s:%d %s] server is NULL\n", __FILE__, __LINE__, __func__);
        return NULL;
    }
    // create a new task
    mj_task* new_task = calloc(1, sizeof(*new_task));
    if (new_task == NULL) {
        fprintf(stderr, " ERROR: [%s:%d %s]\n", __FILE__, __LINE__, __func__);
        return NULL;
    }

    // create ctx for the new client
    w_client* new_ctx = calloc(1, sizeof(*new_ctx));
    if (new_ctx == NULL) {
        fprintf(stderr, " ERROR: [%s:%d %s]\n", __FILE__, __LINE__, __func__);
        return NULL;
    }

    // Get the current time for connection timestamp
    struct timespec connect_time;
    clock_gettime(CLOCK_MONOTONIC, &connect_time);

    // Init all fields in the new context
    new_ctx->state = W_CLIENT_READING;
    new_ctx->fd = client_fd;
    new_ctx->server = server;
    new_ctx->client_number = ++server->total_clients; // Assign sequential number

    new_ctx->read_buffer[0] = '\0';
    new_ctx->bytes_read = 0;

    new_ctx->request_body_len = 0;
    new_ctx->request_body_raw = NULL;
    new_ctx->request_body = NULL;
    new_ctx->parsed_request = NULL;

    new_ctx->response_len = 0;
    new_ctx->response_data = NULL;
    new_ctx->response_sent = 0;

    new_ctx->connect_time = connect_time;
    new_ctx->error_code = W_CLIENT_ERROR_NONE;

    // Set task functions and task context
    new_task->create = NULL;
    new_task->run = w_client_run;
    new_task->cleanup = w_client_cleanup;
    new_task->ctx = new_ctx;

    // Increment active client counter
    server->active_count++;

    return new_task;
}

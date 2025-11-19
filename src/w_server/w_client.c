#include "w_client.h"
#include "../utils/ui.h"
#include "http_parser.h"
#include "majjen.h"
#include "string.h"
#include "utils.h"
#include "w_server.h"
#include <errno.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

#define CLIENT_TIMEOUT_SEC 5

// State machine for clients
void w_client_run(mj_scheduler* scheduler, void* ctx) {
    if (scheduler == NULL || ctx == NULL) {
        return;
    }

    w_client* client = (w_client*)ctx;

    switch (client->state) {
    case W_CLIENT_READING:
        // Check for timeout directly (inline, no function call)
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        time_t elapsed_sec = now.tv_sec - client->connect_time.tv_sec;
        if (elapsed_sec > CLIENT_TIMEOUT_SEC ||
            (elapsed_sec == CLIENT_TIMEOUT_SEC && now.tv_nsec >= client->connect_time.tv_nsec)) {
            ui_print_timeout(client, CLIENT_TIMEOUT_SEC);
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
            ui_print_read_error(client, strerror(errno));
            client->error_code = W_CLIENT_ERROR_READ;
            client->state = W_CLIENT_DONE;
            return;
        }

        // Client closed connection (FIN received)
        if (bytes == 0) {
            ui_print_connection_closed_by_client(client);
            client->state = W_CLIENT_DONE;
            return;
        }

        // fprintf(stderr, "%sClient %4zu (active: %4zu, total: %4zu) Received %zd bytes (total: %zu)%s\n", color,
        // client->client_number, client->server->active_count, client->server->total_clients, bytes,
        // client->bytes_read + bytes, COLOR_RESET);
        ui_print_received_bytes(client, bytes);

        // Update bytes read and null-terminate
        client->bytes_read += bytes;
        client->read_buffer[client->bytes_read] =
            '\0'; // Check if we have a complete HTTP request (ends with \r\n\r\n).
        // We must wait for completed request since current parser does not support incremental parsing.
        // NOTE, this check is "message framing" so it does not belong in parsing
        ui_print_received_bytes(client, bytes);
        if (strstr(client->read_buffer, "\r\n\r\n") != NULL) {
            client->state = W_CLIENT_PARSING;
        } else if (client->bytes_read >= sizeof(client->read_buffer) - 1) {
            // Buffer full but no complete request
            ui_print_request_too_large(client);

            // Send 413 Content Too Large response
            http_response* too_large = http_response_new(RESPONSE_CODE_CONTENT_TOO_LARGE, "Request too large");
            http_response_add_header(too_large, "Content-Type", "text/plain");
            http_response_add_header(too_large, "Connection", "close");
            const char* response_str = http_response_tostring(too_large);
            client->response_data = (char*)response_str;
            client->response_len = strlen(response_str);
            client->response_sent = 0;
            http_response_dispose(&too_large);

            ui_print_response_details(client, 413, "Request Entity Too Large", client->response_len);

            client->error_code = W_CLIENT_ERROR_REQUEST_TOO_LARGE;
            client->state = W_CLIENT_SENDING;
        }
        break;

    case W_CLIENT_PARSING:
        // Parse the complete HTTP request
        http_request* parsed = http_request_fromstring(client->read_buffer);

        if (!parsed || !parsed->valid) {
            // Parse error - send 400 Bad Request
            ui_print_bad_request(client);
            if (parsed)
                http_request_dispose(&parsed);

            // Send 400 Bad Request response
            http_response* bad_request = http_response_new(RESPONSE_CODE_BAD_REQUEST, "Malformed HTTP request");
            http_response_add_header(bad_request, "Content-Type", "text/plain");
            http_response_add_header(bad_request, "Connection", "close");
            const char* response_str = http_response_tostring(bad_request);
            client->response_data = (char*)response_str;
            client->response_len = strlen(response_str);
            client->response_sent = 0;
            http_response_dispose(&bad_request);

            client->error_code = W_CLIENT_ERROR_MALFORMED_REQUEST;
            client->state = W_CLIENT_SENDING;
            return;
        }

        // Store parsed request in client context (cast to void* as per struct)
        client->parsed_request = parsed;

        // Log the parsed request
        ui_print_request_details(client);
        client->state = W_CLIENT_PROCESSING;
        break;

    case W_CLIENT_PROCESSING: {
        // Route request and generate response (backend not implemented yet)
        http_request* req = (http_request*)client->parsed_request;
        http_response* response = NULL;
        ui_print_processing_request(client);
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
        ui_print_response_details(client, response->code, response_code_tostring(response->code), client->response_len);
        // Clean up response struct (string is already copied)
        http_response_dispose(&response);

        client->state = W_CLIENT_SENDING;
        break;
    }

    case W_CLIENT_SENDING:
        // Send response data to client
        if (client->response_data && client->response_sent < client->response_len) {
            ssize_t sent = send(client->fd, client->response_data + client->response_sent,
                                client->response_len - client->response_sent, MSG_DONTWAIT);

            if (sent < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    // Socket buffer full, try again later
                    return;
                }
                // Send error
                ui_print_send_error(client, strerror(errno));
                client->state = W_CLIENT_DONE;
                return;
            }

            if (sent == 0) {
                // Connection closed by peer
                ui_print_connection_closed_during_send(client);
                client->state = W_CLIENT_DONE;
                return;
            }

            client->response_sent += sent;

            // Check if we've sent everything
            if (client->response_sent >= client->response_len) {
                client->state = W_CLIENT_DONE;
            }
        } else {
            // No response data or already sent
            client->state = W_CLIENT_DONE;
        }
        break;

    case W_CLIENT_DONE:
        mj_scheduler_task_remove_current(scheduler); /* invokes w_client_cleanup() */
        break;

    default: /* defensive path */
        ui_print_unknown_state_error(client, client->state);
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
        ui_print_creation_error(__FILE__, __LINE__, __func__);
        return NULL;
    }
    if (server == NULL) {
        ui_print_creation_error_with_msg(__FILE__, __LINE__, __func__, "server is NULL");
        return NULL;
    }
    // create a new task
    mj_task* new_task = calloc(1, sizeof(*new_task));
    if (new_task == NULL) {
        ui_print_creation_error(__FILE__, __LINE__, __func__);
        return NULL;
    }

    // create ctx for the new client
    w_client* new_ctx = calloc(1, sizeof(*new_ctx));
    if (new_ctx == NULL) {
        ui_print_creation_error(__FILE__, __LINE__, __func__);
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

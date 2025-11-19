#include "w_client.h"
#include "../utils/ui.h"
#include "backends/cities/cities.h"
#include "backends/surprise/surprise.h"
#include "backends/weather/weather.h"
#include "http_parser.h"
#include "majjen.h"
#include "string.h"
#include "utils.h"
#include "w_server.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#define CLIENT_TIMEOUT_SEC 5

// Forward declaration of backend done callback
static void w_client_backend_done(void* ctx);

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
        // Route request and dispatch to appropriate backend
        http_request* req = (http_request*)client->parsed_request;
        ui_print_processing_request(client);

        // Initialize backend structure
        w_client_backend* backend = &client->backend;
        backend->backend_struct = NULL;
        backend->backend_get_buffer = NULL;
        backend->backend_get_buffer_size = NULL;
        backend->backend_work = NULL;
        backend->backend_dispose = NULL;
        backend->binary_mode = 0;

        // Route based on URL path
        if (req->method == REQUEST_METHOD_GET && strcmp(req->url, "/GetCities") == 0) {
            cities_init((void*)client, &backend->backend_struct, w_client_backend_done);
            backend->backend_get_buffer = cities_get_buffer;
            backend->backend_work = cities_work;
            backend->backend_dispose = cities_dispose;
            backend->binary_mode = 0;
            client->state = W_CLIENT_BACKEND_WORKING;
        } else if (req->method == REQUEST_METHOD_GET && strncmp(req->url, "/GetWeather", 11) == 0) {
            weather_init((void*)client, &backend->backend_struct, w_client_backend_done);
            backend->backend_get_buffer = weather_get_buffer;
            backend->backend_work = weather_work;
            backend->backend_dispose = weather_dispose;
            backend->binary_mode = 0;

            // Parse query parameters for latitude and longitude
            const char* query_start = strchr(req->url, '?');
            double latitude = 0.0, longitude = 0.0;
            int has_lat = 0, has_lon = 0;

            if (query_start) {
                char* query_copy = strdup(query_start + 1);
                char* token = strtok(query_copy, "&");
                while (token) {
                    if (strncmp(token, "lat=", 4) == 0) {
                        latitude = strtod(token + 4, NULL);
                        has_lat = 1;
                    } else if (strncmp(token, "lon=", 4) == 0) {
                        longitude = strtod(token + 4, NULL);
                        has_lon = 1;
                    }
                    token = strtok(NULL, "&");
                }
                free(query_copy);
            }

            if (!has_lat || !has_lon) {
                // Missing required parameters
                if (backend->backend_struct && backend->backend_dispose) {
                    backend->backend_dispose(&backend->backend_struct);
                }
                http_response* response = http_response_new(RESPONSE_CODE_BAD_REQUEST, "Missing lat or lon parameter");
                http_response_add_header(response, "Content-Type", "text/plain");
                http_response_add_header(response, "Connection", "close");
                const char* response_str = http_response_tostring(response);
                client->response_data = (char*)response_str;
                client->response_len = strlen(response_str);
                client->response_sent = 0;
                http_response_dispose(&response);
                ui_print_response_details(client, 400, "Bad Request", client->response_len);
                client->state = W_CLIENT_SENDING;
                break;
            }

            weather_set_location(&backend->backend_struct, latitude, longitude);
            client->state = W_CLIENT_BACKEND_WORKING;
        } else if (req->method == REQUEST_METHOD_GET && strcmp(req->url, "/GetSurprise") == 0) {
            surprise_init((void*)client, &backend->backend_struct, w_client_backend_done);
            backend->backend_get_buffer = surprise_get_buffer;
            backend->backend_get_buffer_size = surprise_get_buffer_size;
            backend->backend_work = surprise_work;
            backend->backend_dispose = surprise_dispose;
            backend->binary_mode = 1;
            client->state = W_CLIENT_BACKEND_WORKING;
        } else if (req->method == REQUEST_METHOD_GET && strcmp(req->url, "/") == 0) {
            // Root endpoint - simple hello
            http_response* response = http_response_new(RESPONSE_CODE_OK, "Hello from weather server!");
            http_response_add_header(response, "Content-Type", "text/plain");
            http_response_add_header(response, "Connection", "close");
            const char* response_str = http_response_tostring(response);
            client->response_data = (char*)response_str;
            client->response_len = strlen(response_str);
            client->response_sent = 0;
            http_response_dispose(&response);
            ui_print_response_details(client, 200, "OK", client->response_len);
            client->state = W_CLIENT_SENDING;
        } else {
            // 404 for all other routes
            http_response* response = http_response_new(RESPONSE_CODE_NOT_FOUND, "Not found");
            http_response_add_header(response, "Content-Type", "text/plain");
            http_response_add_header(response, "Connection", "close");
            const char* response_str = http_response_tostring(response);
            client->response_data = (char*)response_str;
            client->response_len = strlen(response_str);
            client->response_sent = 0;
            http_response_dispose(&response);
            ui_print_response_details(client, 404, "Not Found", client->response_len);
            client->state = W_CLIENT_SENDING;
        }
        break;
    }

    case W_CLIENT_BACKEND_WORKING: {
        // Let the backend do its work
        w_client_backend* backend = &client->backend;
        if (backend->backend_work && backend->backend_struct) {
            backend->backend_work(&backend->backend_struct);
        }
        // State will be changed by backend when done (via w_client_backend_done callback)
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

// Backend completion callback
static void w_client_backend_done(void* ctx) {
    w_client* client = (w_client*)ctx;
    if (!client)
        return;

    // Get the backend response
    w_client_backend* backend = &client->backend;
    char* buffer = NULL;

    if (backend->backend_get_buffer && backend->backend_struct) {
        backend->backend_get_buffer(&backend->backend_struct, &buffer);
    }

    if (!buffer) {
        // Backend failed to produce response
        http_response* response = http_response_new(RESPONSE_CODE_INTERNAL_SERVER_ERROR, "Backend error");
        http_response_add_header(response, "Content-Type", "text/plain");
        http_response_add_header(response, "Connection", "close");
        const char* response_str = http_response_tostring(response);
        client->response_data = (char*)response_str;
        client->response_len = strlen(response_str);
        client->response_sent = 0;
        http_response_dispose(&response);
        ui_print_response_details(client, 500, "Internal Server Error", client->response_len);
        client->state = W_CLIENT_SENDING;
        return;
    }

    // Generate HTTP response based on backend type
    if (backend->binary_mode) {
        // Binary response (e.g., image)
        size_t buffer_size = 0;
        if (backend->backend_get_buffer_size && backend->backend_struct) {
            backend->backend_get_buffer_size(&backend->backend_struct, &buffer_size);
        }

        // Create raw HTTP response for binary data
        const char* header_template =
            "HTTP/1.1 200 OK\r\nContent-Type: image/png\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n";
        size_t header_len = snprintf(NULL, 0, header_template, buffer_size) + 1;
        char* header = malloc(header_len);
        snprintf(header, header_len, header_template, buffer_size);

        // Combine header and body
        size_t total_len = header_len - 1 + buffer_size;
        client->response_data = malloc(total_len);
        memcpy(client->response_data, header, header_len - 1);
        memcpy(client->response_data + header_len - 1, buffer, buffer_size);
        client->response_len = total_len;
        client->response_sent = 0;
        free(header);
        ui_print_response_details(client, 200, "OK", buffer_size);
    } else {
        // Text/JSON response
        http_response* response = http_response_new(RESPONSE_CODE_OK, buffer);
        http_response_add_header(response, "Content-Type", "application/json");
        http_response_add_header(response, "Connection", "close");
        const char* response_str = http_response_tostring(response);
        client->response_data = (char*)response_str;
        client->response_len = strlen(response_str);
        client->response_sent = 0;
        http_response_dispose(&response);
        ui_print_response_details(client, 200, "OK", strlen(buffer));
    }

    client->state = W_CLIENT_SENDING;
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

    // Dispose backend if active
    if (client->backend.backend_struct && client->backend.backend_dispose) {
        client->backend.backend_dispose(&client->backend.backend_struct);
        client->backend.backend_struct = NULL;
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

    new_ctx->backend.backend_struct = NULL;
    new_ctx->backend.backend_get_buffer = NULL;
    new_ctx->backend.backend_get_buffer_size = NULL;
    new_ctx->backend.backend_work = NULL;
    new_ctx->backend.backend_dispose = NULL;
    new_ctx->backend.binary_mode = 0;

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

#include "w_client.h"
#include "../utils/ui.h"
#include "backends/geocode_weather/geocache.h"
#include "backends/geocode_weather/geocode_weather.h"
#include "global_defines.h"
#include "http_msg_builder.h"
#include "http_parser.h"
#include "majjen.h"
#include "string.h"
#include "utils.h"
#include "w_server.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

static void response_send(w_client* c) {
    /* Kanske intressant om blocking
    "You can set SO_SNDTIMEO via setsockopt() to make send() block only for a limited time"
    */
    int retries = 0;
    ssize_t remaining = c->response_len - c->response_sent;
    // if nothing to send, return
    if (remaining == 0) {
        return;
    }

    ssize_t bytes; // cant have declaration directly after a label (retry:)
retry:
    // no MSG_NOSIGNAL prevents process to be killed if writing to closed socket
    bytes = send(c->fd, c->response_body + c->response_sent, remaining, MSG_NOSIGNAL);

    // handle errors
    if (bytes < 0) {
        switch (errno) {
        case EAGAIN: // would have blocked
            return;
        case EINTR: // interrupted
            // retry N times
            if (retries < MAX_SEND_RETRIES) {
                retries++;
                goto retry;
            }
            return;
        // ERRORS
        case EPIPE: // Socket was orderly closed by peer, or network error
            c->error_code = W_CLIENT_ERROR_SEND_EPIPE;
            return;

        case ECONNRESET: // Socket was closed incorrectly. Peer crashed or missbehaved
            c->error_code = W_CLIENT_ERROR_SEND_ECONNRESET;
            return;

        case EFAULT: // bad pointer to message buffer
            c->error_code = W_CLIENT_ERROR_SEND_EFAULT;
            return;
        default:
            // All other errno values
            c->error_code = W_CLIENT_ERROR_SEND;
            return;
        }
    }

    if (bytes > 0) {
        // Bytes where sent
        c->response_sent += bytes;
        return;
    }

    // bytes == 0 is rare and an error
    c->error_code = W_CLIENT_ERROR_SEND;
    return;
}

// State machine for clients
void w_client_run(mj_scheduler* scheduler, void* ctx) {
    if (scheduler == NULL || ctx == NULL) {
        return;
    }

    w_client* client = (w_client*)ctx;

    switch (client->state) {
    case W_CLIENT_READING: {
        // Check for timeout
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        // compare now to when client first connected
        time_t elapsed_sec = now.tv_sec - client->connect_time.tv_sec;
        if (elapsed_sec > CLIENT_READING_TIMEOUT_SEC ||
            (elapsed_sec == CLIENT_READING_TIMEOUT_SEC && now.tv_nsec >= client->connect_time.tv_nsec)) {
            ui_print_timeout(client, CLIENT_READING_TIMEOUT_SEC);
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

        // Update bytes read and null-terminate
        client->bytes_read += bytes;
        client->read_buffer[client->bytes_read] = '\0';
        ui_print_received_bytes(client, bytes);

        // Check if we have a complete HTTP request (ends with \r\n\r\n).
        if (strstr(client->read_buffer, "\r\n\r\n") != NULL) {
            ui_print_received_request_raw(client);
            client->state = W_CLIENT_PARSING;
        } else if (client->bytes_read >= sizeof(client->read_buffer) - 1) {
            // Buffer full but no complete request
            ui_print_request_too_large(client);
            client->error_code = W_CLIENT_ERROR_REQUEST_TOO_LARGE;
            client->state = W_CLIENT_SENDING;
        }
        break;
    }

    case W_CLIENT_PARSING: {
        // Parse the complete HTTP request
        http_request* parsed = http_request_fromstring(client->read_buffer);

        if (!parsed || !parsed->valid) {
            // Parse error - send 400 Bad Request
            ui_print_bad_request(client);
            if (parsed)
                http_request_dispose(&parsed);

            client->error_code = W_CLIENT_ERROR_MALFORMED_REQUEST;
            client->state = W_CLIENT_SENDING;
            return;
        }

        // Store parsed request in client context (cast to void* as per struct)
        client->parsed_request = parsed;

        // Log the parsed request
        ui_print_request_details(client);
        client->state = W_CLIENT_PROCESSING;
        // break; Fall through here. Parsing is not enough work to yeild.
    }
    case W_CLIENT_PROCESSING: {
        // Route request and dispatch to appropriate backend
        http_request* req = (http_request*)client->parsed_request;
        ui_print_processing_request(client);

        // Parse city from URL query parameter (e.g., "/weather?city=stockholm")
        int city_in_url = 0;
        if (sscanf(req->url, "%*[^=]=%s", client->requested_city) == 1) {
            city_in_url = 1;
            utils_decode_swedish_chars(client->requested_city);
            utils_to_lowercase(client->requested_city);
        }

        // ROOT ROUTE
        // TODO we can server an index.html here
        if (req->method == REQUEST_METHOD_GET && strcmp(req->url, "/") == 0) {
            client->response_body = http_msg_200_ok_text("Hello from weather server!");
            client->response_len = http_msg_get_total_size(client->response_body);
            client->response_sent = 0;
            ui_print_response_details(client, 200, "OK", client->response_len);
            client->state = W_CLIENT_SENDING;
            break;
        }

        /* HEALTH ROUTE - simple liveness check */
        if (req->method == REQUEST_METHOD_GET && strcmp(req->url, "/health") == 0) {
            client->response_body = http_msg_200_ok_text("OK");
            client->response_len = http_msg_get_total_size(client->response_body);
            client->response_sent = 0;
            ui_print_response_details(client, 200, "OK", client->response_len);
            client->state = W_CLIENT_SENDING;
            break;
        }

        /* INDEX ROUTE - serve a minimal HTML index */
        if (req->method == REQUEST_METHOD_GET && strcmp(req->url, "/index.html") == 0) {
            /* Try to open a local index.html file and serve it. Fall back to
             * embedded HTML if the file cannot be read. */
            FILE* f = fopen("www/index.html", "rb");
            if (f) {
                if (fseek(f, 0, SEEK_END) == 0) {
                    long sz = ftell(f);
                    if (sz >= 0) {
                        if (fseek(f, 0, SEEK_SET) == 0) {
                            char* buf = malloc((size_t)sz + 1);
                            if (buf) {
                                size_t r = fread(buf, 1, (size_t)sz, f);
                                buf[r] = '\0';
                                if (r == (size_t)sz) {
                                    /* Build a proper text/html response */
                                    client->response_body = http_msg_build_response(200, "OK", "text/html", buf, NULL);
                                }
                                free(buf);
                            }
                        }
                    }
                }
                fclose(f);
            }

            if (!client->response_body) {
                /* Fallback embedded HTML when file missing or failed to read */
                const char* html = "<html><head><title>WeatherServer</title></head><body><h1>WeatherServer</"
                                   "h1><p>Welcome. <br> No index.html found.</p></body></html>";
                client->response_body = http_msg_build_response(200, "OK", "text/html", html, NULL);
            }

            if (client->response_body) {
                client->response_len = http_msg_get_total_size(client->response_body);
                client->response_sent = 0;
                ui_print_response_details(client, 200, "OK", client->response_len);
                client->state = W_CLIENT_SENDING;
            } else {
                client->error_code = W_CLIENT_ERROR_INTERNAL;
                client->state = W_CLIENT_SENDING;
            }

            break;
        }

        // WEATHER ROUTE, uses geocode_weather task to get coordinates from location name
        if (req->method == REQUEST_METHOD_GET && city_in_url) {

            // Check geocache first - if we have cached coordinates, pass them to the task
            geocache_entry_t cached_entry;
            int cache_hit = 0;
            if (client->server->geocache) {
                if (geocache_lookup(client->server->geocache, client->requested_city, &cached_entry) == 0) {
                    cache_hit = 1;
                    ui_print_backend_state(client, "GeocodeWeather", "geocache hit");
                }
            }

            // Create geocode_weather task to fetch coordinates then weather
            mj_task* gw_task;
            if (cache_hit) {
                // Pass cached coordinates - task will skip geocode API call
                gw_task = geocode_weather_task_create_with_coords(
                    client, client->server->geocache, cached_entry.latitude, cached_entry.longitude, cached_entry.name);
            } else {
                // No cache hit - task will do full geocode + weather lookup
                gw_task = geocode_weather_task_create(client, client->server->geocache);
            }

            if (!gw_task) {
                client->error_code = W_CLIENT_ERROR_INTERNAL;
                client->state = W_CLIENT_SENDING;
                break;
            }

            // Add task to scheduler - it will set client->response_body when done
            if (mj_scheduler_task_add(scheduler, gw_task) < 0) {
                free(gw_task->ctx);
                free(gw_task);
                client->error_code = W_CLIENT_ERROR_INTERNAL;
                client->state = W_CLIENT_SENDING;
                break;
            }

            // Wait for task to complete
            client->state = W_CLIENT_WAITING_TASK;
            break;
        }

        // SURPRISE ROUTE
        // TODO cache files. Dont open and close files every time.
        if (req->method == REQUEST_METHOD_GET && strcmp(req->url, "/surprise") == 0) {
            FILE* fptr = fopen("www/bonzi.png", "rb"); // "rb" - read binary
            if (!fptr) {
                client->error_code = W_CLIENT_ERROR_ROUTE_SURPRISE;
                client->state = W_CLIENT_DONE;
                break;
            }

            // Calculate file size
            fseek(fptr, 0, SEEK_END);
            long file_size_raw = ftell(fptr);
            if (file_size_raw < 0) {
                fclose(fptr);
                client->error_code = W_CLIENT_ERROR_ROUTE_SURPRISE;
                client->state = W_CLIENT_DONE;
                break;
            }
            size_t file_size = (size_t)file_size_raw;
            fseek(fptr, 0, SEEK_SET);

            uint8_t* buffer = (uint8_t*)malloc(sizeof(uint8_t) * file_size);
            if (!buffer) {
                // Failed to allocated memory
                fclose(fptr);
                client->error_code = W_CLIENT_ERROR_ROUTE_SURPRISE;
                client->state = W_CLIENT_DONE;
                break;
            }

            size_t bytes_read = fread(buffer, 1, file_size, fptr);
            if (bytes_read != file_size) {
                // Failed to read file
                free(buffer);
                fclose(fptr);
                client->error_code = W_CLIENT_ERROR_ROUTE_SURPRISE;
                client->state = W_CLIENT_DONE;
                break;
            }

            // Success!
            fclose(fptr);

            // Build response
            client->response_body = http_msg_200_ok_binary("image/png", buffer, file_size);
            client->response_len = http_msg_get_total_size(client->response_body);

            client->state = W_CLIENT_SENDING;
            break;
        }

        // No valid route matched
        client->error_code = W_CLIENT_ERROR_MALFORMED_REQUEST;
        client->state = W_CLIENT_SENDING;
        break;
    }

    case W_CLIENT_WAITING_TASK: {
        // Waiting for external task (e.g., geocode_weather) to complete.
        // The task will set client->response_body and change state to W_CLIENT_SENDING when done.
        // Nothing to do here - just wait.
        break;
    }

    case W_CLIENT_SENDING: {
        // handle any errors from prev state
        if (client->error_code != W_CLIENT_ERROR_NONE) {
            switch (client->error_code) {
            case W_CLIENT_ERROR_REQUEST_TOO_LARGE: {
                client->response_body = http_msg_413_content_too_large(NULL);
                client->response_len = strlen(client->response_body);
                ui_print_response_details(client, 413, "Request Entity Too Large", client->response_len);
                client->state = W_CLIENT_DONE;
                break;
            }
            case W_CLIENT_ERROR_MALFORMED_REQUEST: {
                client->response_body = http_msg_400_bad_request("Malformed HTTP request");
                client->response_len = strlen(client->response_body);
                ui_print_response_details(client, 400, "Bad Request", client->response_len);
                client->state = W_CLIENT_DONE;
                break;
            }
            case W_CLIENT_ERROR_INTERNAL: {
                client->response_body = http_msg_500_internal_error("Backend error");
                client->response_len = strlen(client->response_body);
                ui_print_response_details(client, 500, "Internal Server Error", client->response_len);
                client->state = W_CLIENT_DONE;
                break;
            }
            default:
                client->response_body = http_msg_500_internal_error("Backend error");
                client->response_len = strlen(client->response_body);
                ui_print_response_details(client, 500, "Internal Server Error", client->response_len);
                client->state = W_CLIENT_DONE;
            }
        }

        // Send response
        response_send(client);

        // send error, we are done
        if (client->error_code != W_CLIENT_ERROR_NONE) {
            client->state = W_CLIENT_DONE;
            break;
        }

        /// Did we send the whole response?
        if (client->response_sent == client->response_len) {
            client->state = W_CLIENT_DONE;
            break;
        }

        // no state change, try again next tick
    }

    case W_CLIENT_DONE: {
        mj_scheduler_task_remove_current(scheduler); // also calls w_client_cleanup()
        break;
    }

    default: {
        ui_print_unknown_state_error(client, client->state);
        client->state = W_CLIENT_DONE;
        break;
    }
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
    if (client->response_body) {
        free(client->response_body);
        client->response_body = NULL;
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

    // TODO här kan man lägga in nya generiska SrvStream istället

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
    new_ctx->response_body = NULL;
    new_ctx->response_sent = 0;

    new_ctx->connect_time = connect_time;
    new_ctx->error_code = W_CLIENT_ERROR_NONE;

    // Set task functions and task context
    new_task->create = NULL;
    new_task->run = w_client_run;
    new_task->destroy = w_client_cleanup;
    new_task->ctx = new_ctx;

    // Increment active client counter
    server->active_count++;

    return new_task;
}

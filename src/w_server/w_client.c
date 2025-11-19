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
            fprintf(stderr, "[Client %d] Connection timed out after %d seconds\n", client->fd, CLIENT_TIMEOUT_SEC);
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
            fprintf(stderr, "[Client %d] Read error: %s\n", client->fd, strerror(errno));
            client->error_code = W_CLIENT_ERROR_READ;
            client->state = W_CLIENT_DONE;
            return;
        }

        // Client closed connection (FIN received)
        if (bytes == 0) {
            fprintf(stderr, "[Client %d] Connection closed by client (bytes_read so far: %zu)\n", client->fd,
                    client->bytes_read);
            client->state = W_CLIENT_DONE;
            return;
        }

        fprintf(stderr, "[Client %d] Received %zd bytes\n", client->fd, bytes);

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
            fprintf(stderr, "[Client %d] Request too large\n", client->fd);
            client->error_code = W_CLIENT_ERROR_REQUEST_TOO_LARGE;
            client->state = W_CLIENT_DONE;
        }
        break;

    case W_CLIENT_PARSING:
        fprintf(stderr, "[Client %d] Parsing message:\n%s", client->fd, client->read_buffer);

        // Parse the complete HTTP request
        HTTPRequest* parsed = HTTPRequest_fromstring(client->read_buffer);

        if (!parsed || !parsed->valid) {
            // Parse error - send 400 Bad Request
            fprintf(stderr, "[Client %d] Parse error: %d\n", client->fd,
                    parsed ? parsed->reason : INVALID_REASON_UNKNOWN);
            if (parsed)
                HTTPRequest_Dispose(&parsed);
            client->error_code = W_CLIENT_ERROR_MALFORMED_REQUEST;
            client->state = W_CLIENT_DONE;
            return;
        }

        // Store parsed request in client context (cast to void* as per struct)
        client->parsed_request = parsed;

        // Prettier logging: Show transformation from raw to structured
        fprintf(stderr, "[Client %d] === PARSING SUCCESSFUL ===\n", client->fd);
        fprintf(stderr, "[Client %d] Raw Input (%zu bytes):\n---\n%s\n---\n", client->fd, client->bytes_read,
                client->read_buffer);
        fprintf(stderr, "[Client %d] Parsed Request:\n", client->fd);
        fprintf(stderr, "[Client %d]   Method: %s\n", client->fd, RequestMethod_tostring(parsed->method));
        fprintf(stderr, "[Client %d]   Protocol: HTTP/%d.%d\n", client->fd, (parsed->protocol / 10),
                (parsed->protocol % 10));
        fprintf(stderr, "[Client %d]   URL: %s\n", client->fd, parsed->URL);
        // Iterate over headers using LinkedList_foreach macro
        if (parsed->headers && parsed->headers->size > 0) {
            fprintf(stderr, "[Client %d]   Headers (%zu):\n", client->fd, parsed->headers->size);
            LinkedList_foreach(parsed->headers, node) {
                HTTPHeader* hdr = (HTTPHeader*)node->item;
                fprintf(stderr, "[Client %d]     %s: %s\n", client->fd, hdr->Name, hdr->Value);
            }
        } else {
            fprintf(stderr, "[Client %d]   Headers: (none)\n", client->fd);
        }
        fprintf(stderr, "[Client %d] === END PARSED REQUEST ===\n", client->fd);

        client->state = W_CLIENT_PROCESSING;
        break;

    case W_CLIENT_PROCESSING:
        client->state = W_CLIENT_SENDING;
        break;

    case W_CLIENT_SENDING:
        client->state = W_CLIENT_DONE;
        break;

    case W_CLIENT_DONE:
        mj_scheduler_task_remove_current(scheduler); /* invokes w_client_cleanup() */
        break;

    default: /* defensive path */
        fprintf(stderr, "[Client %d] UNKNOWN STATE %d! Forcing cleanup.\n", client->fd, client->state);
        client->state = W_CLIENT_DONE; /* force a cleanup cycle */
        break;
    }
}

// deallocate any internal reasources added to the context
void w_client_cleanup(mj_scheduler* scheduler, void* ctx) {
    w_client* client = (w_client*)ctx;

    // close socket
    if (client->fd >= 0) {
        shutdown(client->fd, SHUT_WR);
        close(client->fd);
        client->fd = -1;
    }

    // Free parsed HTTP request
    if (client->parsed_request) {
        HTTPRequest* req = (HTTPRequest*)client->parsed_request;
        HTTPRequest_Dispose(&req);
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
mj_task* w_client_create(int client_fd) {
    if (client_fd < 0) {
        fprintf(stderr, " ERROR: [%s:%d %s]\n", __FILE__, __LINE__, __func__);
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

    return new_task;
}

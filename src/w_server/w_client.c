#include "w_client.h"
#include "majjen.h"
#include "sleep_ms.h"
#include "string.h"
#include "w_server.h"
#include <errno.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

#define DEBUG_SLEEP_MS 10

// State machine for clients
void w_client_run(mj_scheduler* scheduler, void* ctx) {
    if (scheduler == NULL || ctx == NULL) {
        return;
    }

    w_client* client = (w_client*)ctx;

    switch (client->state) {
    case W_CLIENT_READING:
        // printf("[Client %d] READING\n", client->fd);
        // TODO: Read message
        printf("Client %d: CONNECTED\n", client->fd);
        sleep_ms(DEBUG_SLEEP_MS);
        client->state = W_CLIENT_PARSING;
        break;

    case W_CLIENT_PARSING:
        //  printf("[Client %d] PARSING\n", client->fd);
        // TODO: Prase message
        sleep_ms(DEBUG_SLEEP_MS);

        client->state = W_CLIENT_PROCESSING;
        break;

    case W_CLIENT_PROCESSING:
        //   printf("[Client %d] PROCESSING\n", client->fd);
        // TODO: Handle the request, e.g., GET /weather?city=Stockholm
        printf("Client %d: PROCESSING\n", client->fd);

        sleep_ms(DEBUG_SLEEP_MS * 10);

        client->state = W_CLIENT_SENDING;
        break;

    case W_CLIENT_SENDING:
        // reply with payload or error

        // ALLT I DET HÄR CASET ÄR BRA FÖR ATT TESTA
        if (client->response_data == NULL) {
            const char* msg = "DONE. CLOSING CONNECTION\n";
            size_t msg_len = strlen(msg);

            client->response_data = malloc(msg_len);
            if (!client->response_data) {
                perror("malloc");
                client->state = W_CLIENT_DONE;
                break;
            }
            memcpy(client->response_data, msg, msg_len);
            client->response_len = msg_len;
            client->response_sent = 0;
        }

        // Attempt to send remaining bytes
        ssize_t n = send(client->fd, client->response_data + client->response_sent,
                         client->response_len - client->response_sent, 0);

        if (n > 0) {
            client->response_sent += n;
        } else if (n < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                perror("send");
                client->state = W_CLIENT_DONE;
            }
            break; // try again next scheduler tick
        }

        // Check if full response sent
        sleep_ms(DEBUG_SLEEP_MS);

        if (client->response_sent >= client->response_len) {
            client->state = W_CLIENT_DONE;
        }
        break;

    case W_CLIENT_DONE:
        // Close connection, free resources and remove task
        // printf("[Client %d]DONE\n", client->fd);

        // Clean up client, remove task from scheduler
        sleep_ms(DEBUG_SLEEP_MS);

        printf("Client %d: DONE, REMOVING\n", client->fd);
        shutdown(client->fd, SHUT_WR);
        mj_scheduler_task_remove_current(scheduler); // This also invokes w_client_cleanup()

        break;

    default:
        fprintf(stderr, "[Client %d] Unknown state %d! Forcing cleanup.\n", client->fd, client->state);
        client->state = W_CLIENT_DONE; // force cleanup
        break;
    }
}

// deallocate any internal reasources added to the context
void w_client_cleanup(mj_scheduler* scheduler, void* ctx) {
    w_client* client = (w_client*)ctx;
    // close socket
    if (client->fd >= 0) {
        close(client->fd);
        client->fd = -1;
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

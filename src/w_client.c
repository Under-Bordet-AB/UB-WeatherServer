#include "w_client.h"
#include "majjen.h"
#include "w_server.h"
#include <stdio.h>

int w_server_client_state_machine_func(mj_scheduler* scheduler, void* state) {

    w_client* client = (w_client*)state;

    printf("CLIENT ACCEPTED WITH  FD = %d\n", client->client_fd);

    /*
    switch (client->state) {
    case W_CLIENT_READING:
        // Try to read

        ssize_t n = read(client->fd, client->read_buffer + client->read_pos,
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
        ssize_t written =
            write(client->fd, client->write_buffer + client->write_pos, client->write_total - client->write_pos);

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
    } */

    // Client is done, remove task and free state
    mj_scheduler_task_remove_current(scheduler);
    return 0;
}
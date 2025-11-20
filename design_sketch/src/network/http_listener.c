#include "../../../src/libs/majjen.h" // Use the real scheduler
#include "../../include/network/connection.h"
#include "../../include/network/listener.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

// ==========================================
// HTTP Connection FSM Logic
// ==========================================

// States for the HTTP Connection
enum {
    HTTP_STATE_INIT,
    HTTP_STATE_READ_REQUEST,
    HTTP_STATE_PROCESS,
    HTTP_STATE_WRITE_RESPONSE,
    HTTP_STATE_CLOSE
};

// Forward declarations of state handlers
static state_id http_on_init(fsm* fsm, ub_context* ctx, void* event);
static state_id http_on_read(fsm* fsm, ub_context* ctx, void* event);
static state_id http_on_process(fsm* fsm, ub_context* ctx, void* event);
static state_id http_on_write(fsm* fsm, ub_context* ctx, void* event);

static fsm_state http_states[] = {{HTTP_STATE_INIT, "INIT", http_on_init, NULL, NULL},
                                  {HTTP_STATE_READ_REQUEST, "READ", NULL, http_on_read, NULL},
                                  {HTTP_STATE_PROCESS, "PROCESS", http_on_process, NULL, NULL},
                                  {HTTP_STATE_WRITE_RESPONSE, "WRITE", NULL, http_on_write, NULL},
                                  {HTTP_STATE_CLOSE, "CLOSE", NULL, NULL, NULL}};

// --- State Handlers ---

static state_id http_on_init(fsm* fsm, ub_context* ctx, void* event) {
    // Prepare for reading
    return HTTP_STATE_READ_REQUEST;
}

static state_id http_on_read(fsm* fsm, ub_context* ctx, void* event) {
    connection* conn = (connection*)ctx->user_data;

    // In a real implementation, 'event' might contain the fd readiness info
    // For this sketch, we assume we are called when data is available.

    ssize_t n = read(conn->fd, conn->read_buffer + conn->read_pos, conn->read_buffer_size - conn->read_pos);

    if (n > 0) {
        conn->read_pos += n;
        // Check if we have a full HTTP request (e.g. look for \r\n\r\n)
        if (strstr(conn->read_buffer, "\r\n\r\n")) {
            return HTTP_STATE_PROCESS;
        }
        return HTTP_STATE_READ_REQUEST; // Keep reading
    } else if (n == 0) {
        return HTTP_STATE_CLOSE; // Client closed
    } else {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return HTTP_STATE_READ_REQUEST; // Wait for more
        }
        return HTTP_STATE_CLOSE; // Error
    }
}

static state_id http_on_process(fsm* fsm, ub_context* ctx, void* event) {
    connection* conn = (connection*)ctx->user_data;

    void* request = conn->read_buffer; // Simplified
    void* response = NULL;

    // If this was resumed by an async callback, `event` will be non-NULL
    if (event) {
        // The callback should have populated conn->response_data
        const char* resp_str =
            conn->response_data ? (const char*)conn->response_data : "HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n";
        strncpy(conn->write_buffer, resp_str, conn->write_buffer_size);
        conn->write_pos = strlen(resp_str);

        // Free response_data now that it's copied to write buffer
        if (conn->response_data) {
            free(conn->response_data);
            conn->response_data = NULL;
            conn->response_len = 0;
        }

        return HTTP_STATE_WRITE_RESPONSE;
    }

    filter_status status = ub_pipeline_execute(conn->pipeline, ctx, request, &response);

    if (status == FILTER_WAIT) {
        // Async backend operation pending. The backend callback will resume the FSM.
        return HTTP_STATE_PROCESS;
    }

    if (status == FILTER_STOP) {
        // Response ready (or error). Copy to write buffer.
        const char* resp_str = (const char*)response;
        if (!resp_str)
            resp_str = "HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n";

        strncpy(conn->write_buffer, resp_str, conn->write_buffer_size);
        conn->write_pos = strlen(resp_str);

        return HTTP_STATE_WRITE_RESPONSE;
    }

    return HTTP_STATE_WRITE_RESPONSE;
}

static state_id http_on_write(fsm* fsm, ub_context* ctx, void* event) {
    connection* conn = (connection*)ctx->user_data;

    ssize_t n = write(conn->fd, conn->write_buffer, conn->write_pos);

    if (n >= 0) {
        // Assuming we wrote everything for simplicity
        return HTTP_STATE_CLOSE; // HTTP 1.0 style
    }

    return HTTP_STATE_CLOSE;
}

// ==========================================
// Listener Implementation
// ==========================================

typedef struct http_listener {
    int listen_fd;
    mj_task* listen_task;
    pipeline* default_pipeline; // assigned by caller via setter
} http_listener;

// Task callback for the scheduler
static void accept_task(mj_scheduler* scheduler, void* ctx) {
    listener* self = (listener*)ctx;
    http_listener* impl = (http_listener*)self->impl_data;

    // Accept loop
    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(impl->listen_fd, (struct sockaddr*)&client_addr, &client_len);

        if (client_fd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                break;
            perror("accept");
            break;
        }

        // Make non-blocking
        int flags = fcntl(client_fd, F_GETFL, 0);
        fcntl(client_fd, F_SETFL, flags | O_NONBLOCK);

        // Create Connection
        connection* conn = ub_connection_create(client_fd, scheduler);
        // Attach default pipeline if set
        if (impl->default_pipeline)
            conn->pipeline = impl->default_pipeline;

        // Initialize FSM for HTTP
        ub_fsm_init(&conn->fsm, http_states, 5, HTTP_STATE_INIT);

        // Create a task for this connection
        // In a real app, we'd have a separate task function that calls ub_fsm_handle_event
        // For now, we just log it.
        printf("New connection accepted: %d\n", client_fd);

        // TODO: Add connection task to scheduler
        // mj_task* conn_task = ...
        // mj_scheduler_task_add(scheduler, conn_task);
    }
}

static int http_init(listener* self, const char* addr, const char* port) {
    http_listener* impl = (http_listener*)self->impl_data;

    impl->listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (impl->listen_fd < 0)
        return -1;

    int opt = 1;
    setsockopt(impl->listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in sin;
    sin.sin_family = AF_INET;
    sin.sin_port = htons(atoi(port));
    sin.sin_addr.s_addr = INADDR_ANY; // Simplified

    if (bind(impl->listen_fd, (struct sockaddr*)&sin, sizeof(sin)) < 0) {
        perror("bind");
        return -1;
    }

    if (listen(impl->listen_fd, 128) < 0) {
        perror("listen");
        return -1;
    }

    // Non-blocking
    int flags = fcntl(impl->listen_fd, F_GETFL, 0);
    fcntl(impl->listen_fd, F_SETFL, flags | O_NONBLOCK);

    return 0;
}

static int http_start(listener* self, mj_scheduler* scheduler) {
    http_listener* impl = (http_listener*)self->impl_data;

    // Create task
    impl->listen_task = malloc(sizeof(mj_task));
    impl->listen_task->run = accept_task;
    impl->listen_task->ctx = self;

    mj_scheduler_task_add(scheduler, impl->listen_task);
    mj_scheduler_update_highest_fd(scheduler, impl->listen_fd);

    return 0;
}

static int http_stop(listener* self) {
    // Remove task from scheduler (not implemented in this sketch)
    return 0;
}

static void http_destroy(listener* self) {
    http_listener* impl = (http_listener*)self->impl_data;
    if (impl->listen_fd >= 0)
        close(impl->listen_fd);
    free(impl);
    free(self);
}

listener* ub_create_http_listener(void) {
    listener* l = malloc(sizeof(listener));
    http_listener* impl = malloc(sizeof(http_listener));

    impl->listen_fd = -1;
    impl->listen_task = NULL;
    impl->default_pipeline = NULL;

    l->impl_data = impl;
    l->init = http_init;
    l->start = http_start;
    l->stop = http_stop;
    l->destroy = http_destroy;

    return l;
}

void ub_http_listener_set_pipeline(listener* l, pipeline* pipeline) {
    if (!l)
        return;
    http_listener* impl = (http_listener*)l->impl_data;
    if (!impl)
        return;
    impl->default_pipeline = pipeline;
}

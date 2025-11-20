#include "../../include/network/connection.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// Default buffer size
#define DEFAULT_BUFFER_SIZE 4096

connection* ub_connection_create(int fd, mj_scheduler* scheduler) {
    connection* conn = malloc(sizeof(connection));
    if (!conn)
        return NULL;

    memset(conn, 0, sizeof(connection));
    conn->fd = fd;
    conn->scheduler = scheduler;

    // Initialize buffers
    conn->read_buffer = malloc(DEFAULT_BUFFER_SIZE);
    conn->read_buffer_size = DEFAULT_BUFFER_SIZE;
    conn->write_buffer = malloc(DEFAULT_BUFFER_SIZE);
    conn->write_buffer_size = DEFAULT_BUFFER_SIZE;

    conn->response_data = NULL;
    conn->response_len = 0;

    // Initialize Pipeline
    conn->pipeline = ub_pipeline_create();

    // Initialize Context
    // In a real app, we'd generate a UUID
    static uint64_t req_id_counter = 0;
    conn->context.request_id = ++req_id_counter;
    conn->context.user_data = conn; // Point back to connection

    return conn;
}

void ub_connection_start(connection* conn) {
    // This would trigger the FSM's initial state
    // ub_fsm_init(&conn->fsm, ...);
    // But the states are usually defined by the specific protocol (HTTP, etc)
    // So the listener usually configures the FSM states before calling start.

    // For now, we assume FSM is configured by the caller (Listener)
    // We just trigger the first event or enter.

    // Trigger initial enter
    // (Assuming ub_fsm_init was called by the listener with the correct states)
}

void ub_connection_destroy(connection* conn) {
    if (!conn)
        return;

    if (conn->fd >= 0) {
        close(conn->fd);
    }

    if (conn->pipeline) {
        ub_pipeline_destroy(conn->pipeline);
    }

    if (conn->response_data)
        free(conn->response_data);

    free(conn->read_buffer);
    free(conn->write_buffer);
    free(conn);
}

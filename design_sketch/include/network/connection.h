#ifndef UB_CONNECTION_H
#define UB_CONNECTION_H

#include "../core/context.h"
#include "../processing/fsm.h"
#include "../processing/pipeline.h"

// Forward declaration
typedef struct mj_scheduler mj_scheduler;

/**
 * @brief Connection Structure
 * Represents a single client connection.
 */
typedef struct connection {
    int fd;
    char client_ip[64];

    // The context for this request/connection
    ub_context context;

    // The state machine driving this connection
    fsm fsm;

    // The processing pipeline
    pipeline* pipeline;

    // Buffers
    char* read_buffer;
    size_t read_buffer_size;
    size_t read_pos;

    char* write_buffer;
    size_t write_buffer_size;
    size_t write_pos;

    // Response from async backends (owned by connection after callback)
    void* response_data;
    size_t response_len;

    // Reference to scheduler for rescheduling tasks
    mj_scheduler* scheduler;

    // Cleanup callback
    void (*on_close)(struct connection* self);
    void* user_data; // For listener to store its own tracking data

} connection;

/**
 * @brief Create a new connection
 */
connection* ub_connection_create(int fd, mj_scheduler* scheduler);

/**
 * @brief Start the connection (initializes FSM)
 */
void ub_connection_start(connection* conn);

/**
 * @brief Destroy the connection
 */
void ub_connection_destroy(connection* conn);

#endif // UB_CONNECTION_H

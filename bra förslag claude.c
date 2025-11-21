// State function signatures
typedef enum {
    STATE_READING_HEADERS,
    STATE_PARSING_HEADERS,
    STATE_READING_BODY,
    STATE_VALIDATING,
    STATE_ROUTING,
    STATE_EXECUTING,
    STATE_ASYNC_IO,
    STATE_SENDING,
    STATE_KEEPALIVE,
    STATE_DONE,
    STATE_ERROR
} client_state_t;

// Forward declarations
client_state_t w_client_handle_reading_headers(client_ctx_t* client);
client_state_t w_client_handle_parsing_headers(client_ctx_t* client);
client_state_t w_client_handle_reading_body(client_ctx_t* client);
client_state_t w_client_handle_validating(client_ctx_t* client);
client_state_t w_client_handle_routing(client_ctx_t* client);
client_state_t w_client_handle_executing(client_ctx_t* client);
client_state_t w_client_handle_async_io(client_ctx_t* client);
client_state_t w_client_handle_sending(client_ctx_t* client);
client_state_t w_client_handle_keepalive(client_ctx_t* client);
client_state_t w_client_handle_done(client_ctx_t* client);
client_state_t w_client_handle_error(client_ctx_t* client);

// Client context structure
typedef enum {
    BODY_STRATEGY_NONE,   // No body (GET, HEAD, etc.)
    BODY_STRATEGY_BUFFER, // Small bodies: buffer in memory
    BODY_STRATEGY_FILE,   // Large bodies: stream to temp file
    BODY_STRATEGY_HANDLER // Stream directly to handler
} body_strategy_t;

typedef struct client_ctx {
    int fd;

    // Header handling
    char header_buffer[MAX_HEADER_SIZE]; // e.g., 8KB
    size_t header_used;
    bool headers_complete;

    // Parsed request
    http_request_t* request;

    // Body handling
    body_strategy_t body_strategy;
    size_t content_length;
    size_t body_received;

    union {
        struct {
            char* buffer;
            size_t capacity;
        } buffered;

        struct {
            int fd;
            char path[256];
        } file;

        struct {
            void* handler_ctx;
            void (*chunk_callback)(void* ctx, const char* data, size_t len);
        } streaming;
    } body;

    // Response
    http_response_t* response;
    size_t response_sent;

    // State management
    client_state_t state;
    int error_code;
    bool keep_alive;

} client_ctx_t;

// =============================================================================
// STATE: READING_HEADERS
// =============================================================================
client_state_t w_client_handle_reading_headers(client_ctx_t* client) {
    char temp_buf[4096];
    ssize_t n;

    // Read into temporary buffer
    size_t space = MAX_HEADER_SIZE - client->header_used;
    if (space == 0) {
        client->error_code = 431; // Request Header Fields Too Large
        return STATE_ERROR;
    }

    n = read(client->fd, temp_buf, MIN(sizeof(temp_buf), space));

    if (n > 0) {
        // Append to header buffer
        memcpy(client->header_buffer + client->header_used, temp_buf, n);
        client->header_used += n;

        // Check if headers are complete (look for \r\n\r\n)
        if (client->header_used >= 4) {
            for (size_t i = client->header_used - n; i <= client->header_used - 4; i++) {
                if (memcmp(client->header_buffer + i, "\r\n\r\n", 4) == 0) {
                    client->headers_complete = true;
                    return STATE_PARSING_HEADERS;
                }
            }
        }

        // More data might be available, try reading again
        return STATE_READING_HEADERS;

    } else if (n == 0) {
        // Connection closed before headers complete
        return STATE_DONE;

    } else { // n < 0
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            // No more data available right now, wait for next event
            return STATE_READING_HEADERS;
        } else {
            // Actual error
            client->error_code = 500;
            return STATE_ERROR;
        }
    }
}

// =============================================================================
// STATE: PARSING_HEADERS
// =============================================================================
client_state_t w_client_handle_parsing_headers(client_ctx_t* client) {
    // Parse the complete headers (one shot)
    client->request = http_parse_request(client->header_buffer, client->header_used);

    if (!client->request) {
        client->error_code = 400; // Bad Request
        return STATE_ERROR;
    }

    // Determine if there's a body and how to handle it
    client->content_length = http_get_content_length(client->request);

    if (client->content_length == 0) {
        // No body, skip to validation
        client->body_strategy = BODY_STRATEGY_NONE;
        return STATE_VALIDATING;
    }

    // Decide body handling strategy
    if (client->content_length > MAX_BODY_SIZE) {
        client->error_code = 413; // Payload Too Large
        return STATE_ERROR;
    } else if (client->content_length <= BODY_BUFFER_THRESHOLD) {
        // Small body: buffer in memory
        client->body_strategy = BODY_STRATEGY_BUFFER;
        client->body.buffered.buffer = malloc(client->content_length);
        client->body.buffered.capacity = client->content_length;

        if (!client->body.buffered.buffer) {
            client->error_code = 500;
            return STATE_ERROR;
        }
    } else {
        // Large body: stream to temp file
        client->body_strategy = BODY_STRATEGY_FILE;
        client->body.file.fd = create_temp_file(client->body.file.path);

        if (client->body.file.fd < 0) {
            client->error_code = 500;
            return STATE_ERROR;
        }
    }

    // Check if any body data was already read with headers
    size_t headers_end = find_headers_end(client->header_buffer, client->header_used);
    size_t extra_data = client->header_used - (headers_end + 4);

    if (extra_data > 0) {
        // Some body data was already read, process it
        const char* body_start = client->header_buffer + headers_end + 4;

        if (client->body_strategy == BODY_STRATEGY_BUFFER) {
            memcpy(client->body.buffered.buffer, body_start, extra_data);
        } else if (client->body_strategy == BODY_STRATEGY_FILE) {
            write(client->body.file.fd, body_start, extra_data);
        }

        client->body_received = extra_data;

        if (client->body_received >= client->content_length) {
            // Entire body was in the header read
            return STATE_VALIDATING;
        }
    }

    return STATE_READING_BODY;
}

// =============================================================================
// STATE: READING_BODY
// =============================================================================
client_state_t w_client_handle_reading_body(client_ctx_t* client) {
    char chunk[8192];
    ssize_t n;

    size_t remaining = client->content_length - client->body_received;
    size_t to_read = MIN(sizeof(chunk), remaining);

    n = read(client->fd, chunk, to_read);

    if (n > 0) {
        // Process chunk based on strategy
        switch (client->body_strategy) {
        case BODY_STRATEGY_BUFFER:
            memcpy(client->body.buffered.buffer + client->body_received, chunk, n);
            break;

        case BODY_STRATEGY_FILE:
            if (write(client->body.file.fd, chunk, n) != n) {
                client->error_code = 500;
                return STATE_ERROR;
            }
            break;

        case BODY_STRATEGY_HANDLER:
            client->body.streaming.chunk_callback(client->body.streaming.handler_ctx, chunk, n);
            break;

        default:
            break;
        }

        client->body_received += n;

        // Check if body is complete
        if (client->body_received >= client->content_length) {
            // Close file if using file strategy
            if (client->body_strategy == BODY_STRATEGY_FILE) {
                fsync(client->body.file.fd);
                lseek(client->body.file.fd, 0, SEEK_SET); // Rewind for reading
            }

            return STATE_VALIDATING;
        }

        // More body to read, try again immediately
        return STATE_READING_BODY;

    } else if (n == 0) {
        // Connection closed before body complete
        client->error_code = 400; // Bad Request
        return STATE_ERROR;

    } else { // n < 0
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            // No more data available, wait for next event
            return STATE_READING_BODY;
        } else {
            client->error_code = 500;
            return STATE_ERROR;
        }
    }
}

// =============================================================================
// STATE: VALIDATING
// =============================================================================
client_state_t w_client_handle_validating(client_ctx_t* client) {
    // Validate request headers
    if (!http_validate_method(client->request)) {
        client->error_code = 405; // Method Not Allowed
        return STATE_ERROR;
    }

    if (!http_validate_uri(client->request)) {
        client->error_code = 400; // Bad Request
        return STATE_ERROR;
    }

    // Authentication/authorization
    if (!auth_check(client->request)) {
        client->error_code = 401; // Unauthorized
        return STATE_ERROR;
    }

    // Content-Type validation if body exists
    if (client->content_length > 0) {
        if (!http_validate_content_type(client->request)) {
            client->error_code = 415; // Unsupported Media Type
            return STATE_ERROR;
        }
    }

    return STATE_ROUTING;
}

// =============================================================================
// STATE: ROUTING
// =============================================================================
client_state_t w_client_handle_routing(client_ctx_t* client) {
    // Match request to handler
    route_handler_t* handler = router_match(client->request->method, client->request->uri);

    if (!handler) {
        client->error_code = 404; // Not Found
        return STATE_ERROR;
    }

    // Store handler in request context
    client->request->handler = handler;

    return STATE_EXECUTING;
}

// =============================================================================
// STATE: EXECUTING
// =============================================================================
client_state_t w_client_handle_executing(client_ctx_t* client) {
    route_handler_t* handler = client->request->handler;

    // Prepare body access for handler
    void* body_data = NULL;
    size_t body_size = 0;

    if (client->body_strategy == BODY_STRATEGY_BUFFER) {
        body_data = client->body.buffered.buffer;
        body_size = client->body_received;
    } else if (client->body_strategy == BODY_STRATEGY_FILE) {
        // Handler will read from client->body.file.fd
        body_data = NULL;
        body_size = client->body_received;
    }

    // Execute handler
    client->response = handler->execute(client->request, body_data, body_size, client);

    if (!client->response) {
        client->error_code = 500;
        return STATE_ERROR;
    }

    // Check if handler needs async I/O
    if (client->response->needs_async) {
        return STATE_ASYNC_IO;
    }

    return STATE_SENDING;
}

// =============================================================================
// STATE: ASYNC_IO
// =============================================================================
client_state_t w_client_handle_async_io(client_ctx_t* client) {
    // Handle async database calls, external API calls, etc.
    // This would integrate with your async I/O system (libuv, epoll, etc.)

    if (async_io_complete(client)) {
        return STATE_SENDING;
    }

    // Wait for async operation to complete
    return STATE_ASYNC_IO;
}

// =============================================================================
// STATE: SENDING
// =============================================================================
client_state_t w_client_handle_sending(client_ctx_t* client) {
    // Serialize response if not already done
    if (!client->response->serialized) {
        http_serialize_response(client->response);
    }

    size_t remaining = client->response->total_size - client->response_sent;
    const char* data = client->response->data + client->response_sent;

    ssize_t n = write(client->fd, data, remaining);

    if (n > 0) {
        client->response_sent += n;

        if (client->response_sent >= client->response->total_size) {
            // Response fully sent
            client->keep_alive = http_should_keep_alive(client->request);

            if (client->keep_alive) {
                return STATE_KEEPALIVE;
            } else {
                return STATE_DONE;
            }
        }

        // More to send
        return STATE_SENDING;

    } else if (n == 0) {
        return STATE_DONE;

    } else { // n < 0
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            // Socket buffer full, wait for next event
            return STATE_SENDING;
        } else {
            return STATE_ERROR;
        }
    }
}

// =============================================================================
// STATE: KEEPALIVE
// =============================================================================
client_state_t w_client_handle_keepalive(client_ctx_t* client) {
    // Clean up current request/response
    http_request_free(client->request);
    http_response_free(client->response);

    // Clean up body resources
    if (client->body_strategy == BODY_STRATEGY_BUFFER) {
        free(client->body.buffered.buffer);
    } else if (client->body_strategy == BODY_STRATEGY_FILE) {
        close(client->body.file.fd);
        unlink(client->body.file.path);
    }

    // Reset client state for next request
    client->request = NULL;
    client->response = NULL;
    client->header_used = 0;
    client->headers_complete = false;
    client->content_length = 0;
    client->body_received = 0;
    client->response_sent = 0;
    client->body_strategy = BODY_STRATEGY_NONE;

    // Start reading next request on same connection
    return STATE_READING_HEADERS;
}

// =============================================================================
// STATE: DONE
// =============================================================================
client_state_t w_client_handle_done(client_ctx_t* client) {
    // Clean up resources
    if (client->request) {
        http_request_free(client->request);
    }

    if (client->response) {
        http_response_free(client->response);
    }

    if (client->body_strategy == BODY_STRATEGY_BUFFER && client->body.buffered.buffer) {
        free(client->body.buffered.buffer);
    } else if (client->body_strategy == BODY_STRATEGY_FILE && client->body.file.fd >= 0) {
        close(client->body.file.fd);
        unlink(client->body.file.path);
    }

    // Close connection
    close(client->fd);

    return STATE_DONE;
}

// =============================================================================
// STATE: ERROR
// =============================================================================
client_state_t w_client_handle_error(client_ctx_t* client) {
    // Create error response
    client->response = http_create_error_response(client->error_code);

    // Try to send error response
    http_serialize_response(client->response);

    // Best effort send (ignore errors here)
    write(client->fd, client->response->data, client->response->total_size);

    return STATE_DONE;
}

// =============================================================================
// MAIN STATE MACHINE DRIVER
// =============================================================================
void client_state_machine_run(client_ctx_t* client) {
    client_state_t (*state_handlers[])(client_ctx_t*) = {
        [STATE_READING_HEADERS] = w_client_handle_reading_headers,
        [STATE_PARSING_HEADERS] = w_client_handle_parsing_headers,
        [STATE_READING_BODY] = w_client_handle_reading_body,
        [STATE_VALIDATING] = w_client_handle_validating,
        [STATE_ROUTING] = w_client_handle_routing,
        [STATE_EXECUTING] = w_client_handle_executing,
        [STATE_ASYNC_IO] = w_client_handle_async_io,
        [STATE_SENDING] = w_client_handle_sending,
        [STATE_KEEPALIVE] = w_client_handle_keepalive,
        [STATE_DONE] = w_client_handle_done,
        [STATE_ERROR] = w_client_handle_error,
    };

    while (client->state != STATE_DONE) {
        client_state_t next_state = state_handlers[client->state](client);

        // Break if waiting for I/O
        if (next_state == client->state && (next_state == STATE_READING_HEADERS || next_state == STATE_READING_BODY ||
                                            next_state == STATE_SENDING || next_state == STATE_ASYNC_IO)) {
            break; // Return to event loop
        }

        client->state = next_state;
    }
}
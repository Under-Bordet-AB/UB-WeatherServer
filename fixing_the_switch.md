# Refactoring the Client State Machine: From Switch to Function Pointers

## Current Problem

The `w_client_run()` function in `src/w_server/w_client.c` is a 350+ line switch statement that:
- Makes testing individual states difficult
- Is hard to extend with new functionality
- Doesn't support middleware/filters
- Violates the Single Responsibility Principle

```c
void w_client_run(mj_scheduler* scheduler, void* ctx) {
    w_client* client = (w_client*)ctx;
    
    switch (client->state) {
        case W_CLIENT_READING: { /* 40 lines */ break; }
        case W_CLIENT_PARSING: { /* 50 lines */ break; }
        case W_CLIENT_PROCESSING: { /* 150 lines */ break; }
        case W_CLIENT_BACKEND_WORKING: { /* 10 lines */ break; }
        case W_CLIENT_SENDING: { /* 40 lines */ break; }
        case W_CLIENT_DONE: { /* 5 lines */ break; }
    }
}

// NAME SUGGESTIONS FROM CLAUDE
w_client_handle_accepting      // Connection setup, TLS handshake
w_client_handle_reading        // Read from socket
w_client_handle_parsing        // Parse HTTP request
w_client_handle_validating     // Validate request, authentication
w_client_handle_routing        // Match to handler/resource
w_client_handle_executing      // Business logic execution
w_client_handle_async_io       // Async DB/service calls (if needed)
w_client_handle_sending        // Serialize & send response
w_client_handle_keepalive      // Handle persistent connections
w_client_handle_done           // Cleanup / close connection
w_client_handle_error          // Unified error handling
```
## Solution Overview

Replace the switch statement with:
1. **Function pointer lookup table** - Direct state-to-function mapping
2. **Pipeline/filter system** - Composable middleware for cross-cutting concerns

---

## Part 1: Function Pointer Lookup Table

### Step 1: Define the State Handler Signature

In `w_client.h`, add:

```c
// Forward declaration
typedef struct w_client w_client;

// State handler function signature
typedef void (*w_client_state_handler)(w_client* client, mj_scheduler* scheduler);
```

### Step 2: Declare Individual State Functions

In `w_client.h`:

```c
// State handler declarations (implemented in w_client.c)
void w_client_handle_reading(w_client* client, mj_scheduler* scheduler);
void w_client_handle_parsing(w_client* client, mj_scheduler* scheduler);
void w_client_handle_processing(w_client* client, mj_scheduler* scheduler);
void w_client_handle_backend_working(w_client* client, mj_scheduler* scheduler);
void w_client_handle_sending(w_client* client, mj_scheduler* scheduler);
void w_client_handle_done(w_client* client, mj_scheduler* scheduler);
```

### Step 3: Create the Lookup Table

In `w_client.c`:

```c
// Global lookup table mapping states to handlers
static const w_client_state_handler state_handlers[] = {
    [W_CLIENT_READING] = w_client_handle_reading,
    [W_CLIENT_PARSING] = w_client_handle_parsing,
    [W_CLIENT_PROCESSING] = w_client_handle_processing,
    [W_CLIENT_BACKEND_WORKING] = w_client_handle_backend_working,
    [W_CLIENT_SENDING] = w_client_handle_sending,
    [W_CLIENT_DONE] = w_client_handle_done,
};

// Verify array is correctly sized at compile time
_Static_assert(
    sizeof(state_handlers) / sizeof(state_handlers[0]) == W_CLIENT_DONE + 1,
    "State handler table size mismatch"
);
```

### Step 4: Simplify the Main Run Function

Replace the entire switch statement:

```c
void w_client_run(mj_scheduler* scheduler, void* ctx) {
    if (scheduler == NULL || ctx == NULL) {
        return;
    }

    w_client* client = (w_client*)ctx;
    
    // Bounds check
    if (client->state < 0 || client->state > W_CLIENT_DONE) {
        ui_print_unknown_state_error(client, client->state);
        client->state = W_CLIENT_DONE;
        return;
    }
    
    // Dispatch to appropriate handler
    state_handlers[client->state](client, scheduler);
}
```

### Step 5: Extract Each Case to Its Own Function

Move each `case` block into its own function. Example for `W_CLIENT_READING`:

```c
void w_client_handle_reading(w_client* client, mj_scheduler* scheduler) {
    // Check for timeout
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    time_t elapsed_sec = now.tv_sec - client->connect_time.tv_sec;
    
    if (elapsed_sec > CLIENT_READING_TIMEOUT_SEC ||
        (elapsed_sec == CLIENT_READING_TIMEOUT_SEC && 
         now.tv_nsec >= client->connect_time.tv_nsec)) {
        ui_print_timeout(client, CLIENT_READING_TIMEOUT_SEC);
        client->error_code = W_CLIENT_ERROR_TIMEOUT;
        client->state = W_CLIENT_DONE;
        return;
    }

    // Try to read data from the client socket
    ssize_t bytes = recv(client->fd, 
                        client->read_buffer + client->bytes_read,
                        sizeof(client->read_buffer) - client->bytes_read - 1, 
                        0);

    if (bytes < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return; // No data available yet
        }
        ui_print_read_error(client, strerror(errno));
        client->error_code = W_CLIENT_ERROR_READ;
        client->state = W_CLIENT_DONE;
        return;
    }

    if (bytes == 0) {
        ui_print_connection_closed_by_client(client);
        client->state = W_CLIENT_DONE;
        return;
    }

    ui_print_received_bytes(client, bytes);
    client->bytes_read += bytes;
    client->read_buffer[client->bytes_read] = '\0';

    // Check for complete request
    if (strstr(client->read_buffer, "\r\n\r\n") != NULL) {
        client->state = W_CLIENT_PARSING;
    } else if (client->bytes_read >= sizeof(client->read_buffer) - 1) {
        // Buffer full, send 413
        http_response* too_large = http_response_new(
            RESPONSE_CODE_CONTENT_TOO_LARGE, 
            "Request too large"
        );
        http_response_add_header(too_large, "Content-Type", "text/plain");
        http_response_add_header(too_large, "Connection", "close");
        const char* response_str = http_response_tostring(too_large);
        client->response_data = (char*)response_str;
        client->response_len = strlen(response_str);
        client->response_sent = 0;
        http_response_dispose(&too_large);
        
        ui_print_response_details(client, 413, "Request Entity Too Large", 
                                 client->response_len);
        client->error_code = W_CLIENT_ERROR_REQUEST_TOO_LARGE;
        client->state = W_CLIENT_SENDING;
    }
}
```

**Benefits:**
- Each function is testable in isolation
- Easier to understand (single responsibility)
- Can profile individual states
- Cleaner git diffs when modifying states

---

## Part 2: Pipeline/Filter System

### Design Goals

Add composable middleware that can:
- Log requests/responses
- Collect metrics
- Enforce authentication
- Rate limit per IP
- Compress responses
- Modify requests/responses

### Architecture

```
┌─────────────────────────────────────────────┐
│           w_client_run()                    │
│                                             │
│  ┌────────────────────────────────────┐    │
│  │  FOR EACH FILTER IN PIPELINE       │    │
│  │    filter->before(client)          │    │
│  │    IF filter says ABORT → return   │    │
│  └────────────────────────────────────┘    │
│                                             │
│  ┌────────────────────────────────────┐    │
│  │  state_handlers[state](client)     │    │
│  └────────────────────────────────────┘    │
│                                             │
│  ┌────────────────────────────────────┐    │
│  │  FOR EACH FILTER IN PIPELINE       │    │
│  │    filter->after(client)           │    │
│  └────────────────────────────────────┘    │
└─────────────────────────────────────────────┘
```

### Step 1: Define Filter Interface

In `w_client.h`:

```c
// Filter result codes
typedef enum {
    W_CLIENT_FILTER_CONTINUE = 0,  // Continue to next filter/handler
    W_CLIENT_FILTER_ABORT = 1,     // Stop processing, client will be cleaned up
    W_CLIENT_FILTER_SKIP = 2       // Skip state handler but continue filters
} w_client_filter_result;

// Filter context for passing data between filters
typedef struct {
    void* data;        // Filter-specific data
    size_t data_size;
    void (*cleanup)(void* data);
} w_client_filter_context;

// Filter interface
typedef struct w_client_filter {
    const char* name;  // For debugging/logging
    
    // Called before state handler (can modify client, abort request, etc.)
    w_client_filter_result (*before)(
        struct w_client_filter* self,
        w_client* client,
        w_client_filter_context* ctx
    );
    
    // Called after state handler (can log, collect metrics, etc.)
    w_client_filter_result (*after)(
        struct w_client_filter* self,
        w_client* client,
        w_client_filter_context* ctx
    );
    
    // Filter's private data (e.g., config, counters)
    void* filter_data;
    
    // Cleanup function for filter_data
    void (*cleanup)(struct w_client_filter* self);
} w_client_filter;
```

### Step 2: Add Pipeline to Client Context

In `w_client.h`, add to `w_client` struct:

```c
typedef struct w_client {
    // ... existing fields ...
    
    // Filter pipeline
    w_client_filter** filters;      // Array of filter pointers
    size_t filter_count;
    w_client_filter_context* filter_ctx;  // Per-request filter context
} w_client;
```

### Step 3: Update Main Run Function with Pipeline

In `w_client.c`:

```c
void w_client_run(mj_scheduler* scheduler, void* ctx) {
    if (scheduler == NULL || ctx == NULL) {
        return;
    }

    w_client* client = (w_client*)ctx;
    
    // Bounds check
    if (client->state < 0 || client->state > W_CLIENT_DONE) {
        ui_print_unknown_state_error(client, client->state);
        client->state = W_CLIENT_DONE;
        return;
    }
    
    // --- BEFORE FILTERS ---
    int skip_handler = 0;
    for (size_t i = 0; i < client->filter_count; i++) {
        w_client_filter* filter = client->filters[i];
        if (filter && filter->before) {
            w_client_filter_result result = filter->before(
                filter, 
                client, 
                client->filter_ctx
            );
            
            if (result == W_CLIENT_FILTER_ABORT) {
                client->state = W_CLIENT_DONE;
                return;
            } else if (result == W_CLIENT_FILTER_SKIP) {
                skip_handler = 1;
            }
        }
    }
    
    // --- STATE HANDLER ---
    if (!skip_handler) {
        state_handlers[client->state](client, scheduler);
    }
    
    // --- AFTER FILTERS (in reverse order) ---
    for (int i = client->filter_count - 1; i >= 0; i--) {
        w_client_filter* filter = client->filters[i];
        if (filter && filter->after) {
            filter->after(filter, client, client->filter_ctx);
        }
    }
}
```

### Step 4: Example Filter - Request Logger

Create `src/w_server/filters/request_logger.c`:

```c
#include "w_client.h"
#include <stdio.h>
#include <time.h>

typedef struct {
    FILE* log_file;
    struct timespec start_time;
} request_logger_data;

static w_client_filter_result request_logger_before(
    w_client_filter* self,
    w_client* client,
    w_client_filter_context* ctx
) {
    request_logger_data* data = (request_logger_data*)self->filter_data;
    
    // Only log when request is parsed
    if (client->state == W_CLIENT_PARSING && client->parsed_request) {
        http_request* req = (http_request*)client->parsed_request;
        
        // Record start time
        clock_gettime(CLOCK_MONOTONIC, &data->start_time);
        
        fprintf(data->log_file, 
                "[%zu] %s %s - ", 
                client->client_number,
                request_method_tostring(req->method),
                req->url);
        fflush(data->log_file);
    }
    
    return W_CLIENT_FILTER_CONTINUE;
}

static w_client_filter_result request_logger_after(
    w_client_filter* self,
    w_client* client,
    w_client_filter_context* ctx
) {
    request_logger_data* data = (request_logger_data*)self->filter_data;
    
    // Log completion when done sending
    if (client->state == W_CLIENT_DONE) {
        struct timespec end_time;
        clock_gettime(CLOCK_MONOTONIC, &end_time);
        
        double duration_ms = 
            (end_time.tv_sec - data->start_time.tv_sec) * 1000.0 +
            (end_time.tv_nsec - data->start_time.tv_nsec) / 1000000.0;
        
        fprintf(data->log_file, 
                "completed in %.2fms\n", 
                duration_ms);
        fflush(data->log_file);
    }
    
    return W_CLIENT_FILTER_CONTINUE;
}

static void request_logger_cleanup(w_client_filter* self) {
    request_logger_data* data = (request_logger_data*)self->filter_data;
    if (data) {
        if (data->log_file && data->log_file != stdout) {
            fclose(data->log_file);
        }
        free(data);
    }
}

w_client_filter* request_logger_create(const char* log_path) {
    w_client_filter* filter = calloc(1, sizeof(w_client_filter));
    request_logger_data* data = calloc(1, sizeof(request_logger_data));
    
    data->log_file = log_path ? fopen(log_path, "a") : stdout;
    
    filter->name = "request_logger";
    filter->before = request_logger_before;
    filter->after = request_logger_after;
    filter->cleanup = request_logger_cleanup;
    filter->filter_data = data;
    
    return filter;
}
```

### Step 5: Example Filter - Rate Limiter

Create `src/w_server/filters/rate_limiter.c`:

```c
#include "w_client.h"
#include <time.h>
#include <string.h>
#include <arpa/inet.h>

#define MAX_IPS 1024
#define WINDOW_SECONDS 60
#define MAX_REQUESTS_PER_WINDOW 100

typedef struct {
    char ip[46];  // IPv6 address string
    time_t window_start;
    int request_count;
} ip_quota;

typedef struct {
    ip_quota quotas[MAX_IPS];
    size_t quota_count;
    int max_requests;
    int window_seconds;
} rate_limiter_data;

static w_client_filter_result rate_limiter_before(
    w_client_filter* self,
    w_client* client,
    w_client_filter_context* ctx
) {
    rate_limiter_data* data = (rate_limiter_data*)self->filter_data;
    
    // Only check on parsing state
    if (client->state != W_CLIENT_PARSING) {
        return W_CLIENT_FILTER_CONTINUE;
    }
    
    // Get client IP from fd (simplified - real impl needs getpeername)
    const char* client_ip = "127.0.0.1";  // TODO: extract from socket
    
    time_t now = time(NULL);
    ip_quota* quota = NULL;
    
    // Find or create quota entry
    for (size_t i = 0; i < data->quota_count; i++) {
        if (strcmp(data->quotas[i].ip, client_ip) == 0) {
            quota = &data->quotas[i];
            break;
        }
    }
    
    if (!quota && data->quota_count < MAX_IPS) {
        quota = &data->quotas[data->quota_count++];
        strncpy(quota->ip, client_ip, sizeof(quota->ip) - 1);
        quota->window_start = now;
        quota->request_count = 0;
    }
    
    if (!quota) {
        return W_CLIENT_FILTER_CONTINUE;  // Table full, allow request
    }
    
    // Reset window if expired
    if (now - quota->window_start >= data->window_seconds) {
        quota->window_start = now;
        quota->request_count = 0;
    }
    
    // Check limit
    if (quota->request_count >= data->max_requests) {
        // Rate limited! Send 429 response
        http_response* response = http_response_new(
            RESPONSE_CODE_TOO_MANY_REQUESTS,
            "Rate limit exceeded"
        );
        http_response_add_header(response, "Content-Type", "text/plain");
        http_response_add_header(response, "Connection", "close");
        
        char retry_after[32];
        snprintf(retry_after, sizeof(retry_after), "%d", 
                (int)(data->window_seconds - (now - quota->window_start)));
        http_response_add_header(response, "Retry-After", retry_after);
        
        const char* response_str = http_response_tostring(response);
        client->response_data = (char*)response_str;
        client->response_len = strlen(response_str);
        client->response_sent = 0;
        http_response_dispose(&response);
        
        client->state = W_CLIENT_SENDING;
        return W_CLIENT_FILTER_SKIP;  // Skip normal processing
    }
    
    quota->request_count++;
    return W_CLIENT_FILTER_CONTINUE;
}

w_client_filter* rate_limiter_create(int max_requests, int window_seconds) {
    w_client_filter* filter = calloc(1, sizeof(w_client_filter));
    rate_limiter_data* data = calloc(1, sizeof(rate_limiter_data));
    
    data->max_requests = max_requests;
    data->window_seconds = window_seconds;
    data->quota_count = 0;
    
    filter->name = "rate_limiter";
    filter->before = rate_limiter_before;
    filter->after = NULL;  // No after action needed
    filter->cleanup = NULL;  // Just free data
    filter->filter_data = data;
    
    return filter;
}
```

### Step 6: Register Filters

In `w_client_create()`:

```c
mj_task* w_client_create(int client_fd, w_server* server) {
    // ... existing setup ...
    
    // Allocate filter array
    new_ctx->filter_count = 2;
    new_ctx->filters = calloc(new_ctx->filter_count, sizeof(w_client_filter*));
    
    // Install filters (order matters!)
    new_ctx->filters[0] = rate_limiter_create(100, 60);  // First: rate limit
    new_ctx->filters[1] = request_logger_create(NULL);    // Then: log
    
    // Allocate filter context
    new_ctx->filter_ctx = calloc(1, sizeof(w_client_filter_context));
    
    // ... rest of setup ...
}
```

And cleanup in `w_client_cleanup()`:

```c
void w_client_cleanup(mj_scheduler* scheduler, void* ctx) {
    w_client* client = (w_client*)ctx;
    
    // ... existing cleanup ...
    
    // Cleanup filters
    if (client->filters) {
        for (size_t i = 0; i < client->filter_count; i++) {
            if (client->filters[i]) {
                if (client->filters[i]->cleanup) {
                    client->filters[i]->cleanup(client->filters[i]);
                }
                free(client->filters[i]->filter_data);
                free(client->filters[i]);
            }
        }
        free(client->filters);
    }
    
    if (client->filter_ctx) {
        if (client->filter_ctx->cleanup && client->filter_ctx->data) {
            client->filter_ctx->cleanup(client->filter_ctx->data);
        }
        free(client->filter_ctx);
    }
    
    // ... rest of cleanup ...
}
```

---

## Part 3: Testing Strategy

### Testing State Handlers

```c
// test_w_client_states.c
#include "w_client.h"
#include <assert.h>

void test_reading_state_timeout() {
    w_client client = {0};
    client.state = W_CLIENT_READING;
    client.connect_time.tv_sec = 0;
    
    // Mock current time to be past timeout
    // (In real test, you'd inject time via dependency injection)
    
    w_client_handle_reading(&client, NULL);
    
    assert(client.state == W_CLIENT_DONE);
    assert(client.error_code == W_CLIENT_ERROR_TIMEOUT);
}

void test_reading_state_receives_data() {
    w_client client = {0};
    client.fd = /* mock socket fd */;
    client.state = W_CLIENT_READING;
    
    // Set up mock recv() to return test data
    
    w_client_handle_reading(&client, NULL);
    
    assert(client.bytes_read > 0);
    assert(client.state == W_CLIENT_READING);  // Still reading
}
```

### Testing Filters

```c
void test_rate_limiter_allows_first_request() {
    w_client_filter* filter = rate_limiter_create(10, 60);
    w_client client = {0};
    w_client_filter_context ctx = {0};
    
    w_client_filter_result result = filter->before(filter, &client, &ctx);
    
    assert(result == W_CLIENT_FILTER_CONTINUE);
    filter->cleanup(filter);
}

void test_rate_limiter_blocks_excess_requests() {
    w_client_filter* filter = rate_limiter_create(5, 60);
    w_client client = {0};
    w_client_filter_context ctx = {0};
    
    // Make 5 requests (should succeed)
    for (int i = 0; i < 5; i++) {
        w_client_filter_result result = filter->before(filter, &client, &ctx);
        assert(result == W_CLIENT_FILTER_CONTINUE);
    }
    
    // 6th request should be blocked
    w_client_filter_result result = filter->before(filter, &client, &ctx);
    assert(result == W_CLIENT_FILTER_SKIP);
    assert(client.state == W_CLIENT_SENDING);  // Should have 429 response ready
    
    filter->cleanup(filter);
}
```

---

## Part 4: Migration Strategy

**Phase 1: Extract Functions (No behavior change)**
1. Keep existing switch statement
2. Create individual state handler functions
3. Call them from switch cases
4. Test thoroughly

**Phase 2: Add Lookup Table (Minimal risk)**
1. Create `state_handlers[]` array
2. Replace switch with table lookup
3. Verify same behavior

**Phase 3: Add Filter Infrastructure (New feature)**
1. Add filter types to `w_client.h`
2. Add filter array to `w_client` struct
3. Update `w_client_run()` to call filters
4. Initially ship with zero filters

**Phase 4: Implement Filters (Gradual rollout)**
1. Start with logging filter
2. Add metrics filter
3. Add rate limiting
4. Add authentication/authorization

---

## Performance Considerations

**Lookup Table:**
- Array indexing: O(1) vs switch O(1)
- No performance difference in practice
- Slightly better code cache locality

**Filter Pipeline:**
- Overhead: ~50-100ns per filter (function call + condition check)
- For 3 filters: ~300ns additional latency
- Negligible compared to I/O (recv = 1-10μs, disk = 100μs-10ms)
- Can disable filters in production builds with compiler flag

**Memory:**
- Each client: +16 bytes for filter array pointer + count
- Shared filters: ~200 bytes per filter type (not per client)
- Total overhead: <1KB per 1000 clients

---

## Summary

**Before:**
- 350-line switch statement
- Hard to test
- Hard to extend
- No middleware support

**After:**
- 6 focused functions (~50 lines each)
- Each function testable independently
- Composable filter system
- Clean separation of concerns

**Effort:** 1-2 days for refactor, ongoing benefit for all future development.

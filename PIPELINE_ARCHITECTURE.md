# Implementing Pipeline Architecture for HTTP Processing

**Date:** November 18, 2025  
**Status:** Design Proposal  
**Target:** UB-WeatherServer

---

## Overview

This document explains how to refactor the current monolithic state machine in `w_client.c` into a clean **Pipes and Filters** architecture. This pattern will make the HTTP processing flow more modular, testable, and maintainable.

---

## Current Architecture Problem

### The Monolithic State Machine

Right now, the client handler uses a single state machine that tries to do everything:

```c
void w_client_run(mj_scheduler* scheduler, void* ctx) {
    w_client* client = (w_client*)ctx;
    
    switch (client->state) {
        case W_CLIENT_READING:
            // Read bytes from socket
            // Parse HTTP headers
            // Validate content length
            // Check for complete request
            break;
            
        case W_CLIENT_PROCESSING:
            // Route to handler
            // Execute business logic
            // Format response
            // Set headers
            break;
            
        case W_CLIENT_SENDING:
            // Send response
            break;
    }
}
```

**Problems:**
- All logic mixed together
- Hard to test individual stages
- Difficult to add new processing steps
- Can't reuse processing logic
- No clear separation of concerns

---

## The Pipeline Solution

### Conceptual Model

Think of HTTP request processing as water flowing through pipes:

```
┌─────────┐    ┌──────────┐    ┌────────┐    ┌─────────┐    ┌───────────┐
│ Request │───▶│  Parser  │───▶│ Validator│───▶│ Router │───▶│  Handler  │
└─────────┘    └──────────┘    └────────┘    └─────────┘    └─────┬─────┘
                                                                     │
┌──────────┐    ┌───────────┐                                      │
│ Response │◀───│ Formatter │◀─────────────────────────────────────┘
└──────────┘    └───────────┘
```

Each stage:
- Does **one thing** well
- Can be tested independently
- Can be added/removed/reordered
- Can pass data to the next stage or stop the pipeline

---

## Core Data Structures

### 1. Request and Response Objects

First, we need standard objects that flow through the pipeline:

```c
typedef struct w_request {
    // Raw HTTP data
    HTTPRequest* http_request;
    
    // Parsed components
    const char* method;      // "GET", "POST"
    const char* path;        // "/weather"
    const char* query;       // "?city=Stockholm"
    
    // Body data
    const char* body;
    size_t body_len;
    
    // Metadata
    int client_fd;
    void* user_data;        // For passing custom data between filters
} w_request;

typedef struct w_response {
    // Response components
    int status_code;        // 200, 404, 500, etc.
    const char* body;
    size_t body_len;
    
    // Headers to add
    LinkedList* headers;    // List of name/value pairs
    
    // Control flags
    int stop_pipeline;      // Set to 1 to halt further processing
    int connection_close;   // Set to 1 to close after response
} w_response;
```

### 2. Filter Function Signature

Each filter is a function with this signature:

```c
// Return 0 for success (continue pipeline)
// Return non-zero for error (stop pipeline)
typedef int (*w_filter_fn)(w_request* req, w_response* resp);
```

### 3. Pipeline Structure

```c
#define MAX_FILTERS 10

typedef struct w_pipeline {
    w_filter_fn filters[MAX_FILTERS];
    size_t filter_count;
} w_pipeline;
```

---

## Example Filters

### Parser Filter

**Purpose:** Convert raw HTTP bytes into structured request

```c
int filter_parse_http(w_request* req, w_response* resp) {
    // Parse the raw HTTP request
    HTTPRequest* http_req = HTTPRequest_fromstring(req->raw_data);
    
    if (!http_req || !http_req->valid) {
        // Parsing failed - set error response
        resp->status_code = 400;  // Bad Request
        resp->body = "Malformed HTTP request";
        resp->stop_pipeline = 1;  // Don't continue processing
        return -1;
    }
    
    // Extract components
    req->http_request = http_req;
    req->method = RequestMethod_tostring(http_req->method);
    req->path = http_req->URL;
    
    return 0;  // Success - continue to next filter
}
```

### Validator Filter

**Purpose:** Check request validity and enforce limits

```c
int filter_validate_request(w_request* req, w_response* resp) {
    // Check method is supported
    if (strcmp(req->method, "GET") != 0 && 
        strcmp(req->method, "POST") != 0) {
        resp->status_code = 405;  // Method Not Allowed
        resp->body = "Only GET and POST are supported";
        resp->stop_pipeline = 1;
        return -1;
    }
    
    // Check URL length
    if (strlen(req->path) > MAX_URL_LEN) {
        resp->status_code = 414;  // URI Too Long
        resp->body = "URL exceeds maximum length";
        resp->stop_pipeline = 1;
        return -1;
    }
    
    // Check body size for POST
    if (strcmp(req->method, "POST") == 0) {
        if (req->body_len > MAX_BODY_SIZE) {
            resp->status_code = 413;  // Content Too Large
            resp->body = "Request body too large";
            resp->stop_pipeline = 1;
            return -1;
        }
    }
    
    return 0;  // Validation passed
}
```

### Logging Filter

**Purpose:** Record request details (runs before and after)

```c
int filter_log_request(w_request* req, w_response* resp) {
    // Log incoming request
    printf("[%d] %s %s\n", req->client_fd, req->method, req->path);
    
    // Could add timing information
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    req->user_data = malloc(sizeof(struct timespec));
    memcpy(req->user_data, &now, sizeof(struct timespec));
    
    return 0;  // Always continue
}

int filter_log_response(w_request* req, w_response* resp) {
    // Calculate request duration
    struct timespec start = *(struct timespec*)req->user_data;
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    long duration_ms = (now.tv_sec - start.tv_sec) * 1000 + 
                       (now.tv_nsec - start.tv_nsec) / 1000000;
    
    printf("[%d] Response %d - %ld ms\n", 
           req->client_fd, resp->status_code, duration_ms);
    
    free(req->user_data);
    return 0;
}
```

### Router Filter

**Purpose:** Map URL paths to handlers

```c
typedef int (*w_route_handler)(w_request* req, w_response* resp);

typedef struct {
    const char* path;
    const char* method;
    w_route_handler handler;
} w_route;

// Route definitions
int handle_weather(w_request* req, w_response* resp);
int handle_health(w_request* req, w_response* resp);

w_route routes[] = {
    {"/weather", "GET", handle_weather},
    {"/health", "GET", handle_health},
    {NULL, NULL, NULL}
};

int filter_router(w_request* req, w_response* resp) {
    // Find matching route
    for (int i = 0; routes[i].path != NULL; i++) {
        if (strcmp(req->path, routes[i].path) == 0 &&
            strcmp(req->method, routes[i].method) == 0) {
            
            // Call the handler
            return routes[i].handler(req, resp);
        }
    }
    
    // No route found
    resp->status_code = 404;
    resp->body = "Not Found";
    resp->stop_pipeline = 1;
    return -1;
}
```

### Formatter Filter

**Purpose:** Convert response data to proper HTTP format

```c
int filter_format_http(w_request* req, w_response* resp) {
    // Create HTTPResponse object
    HTTPResponse* http_resp = HTTPResponse_new(resp->status_code, resp->body);
    
    // Add standard headers
    HTTPResponse_add_header(http_resp, "Server", "UB-WeatherServer/1.0");
    HTTPResponse_add_header(http_resp, "Content-Type", "text/plain");
    
    char len_str[32];
    snprintf(len_str, sizeof(len_str), "%zu", resp->body_len);
    HTTPResponse_add_header(http_resp, "Content-Length", len_str);
    
    // Add custom headers from response
    if (resp->headers) {
        LinkedList_foreach(resp->headers, node) {
            HTTPHeader* hdr = (HTTPHeader*)node->item;
            HTTPResponse_add_header(http_resp, hdr->Name, hdr->Value);
        }
    }
    
    // Convert to wire format
    const char* formatted = HTTPResponse_tostring(http_resp);
    
    // Replace body with full HTTP response
    resp->body = formatted;
    resp->body_len = strlen(formatted);
    
    return 0;
}
```

---

## Pipeline Execution

### Building the Pipeline

```c
w_pipeline* w_pipeline_create(void) {
    w_pipeline* pipeline = calloc(1, sizeof(w_pipeline));
    if (!pipeline) return NULL;
    
    pipeline->filter_count = 0;
    return pipeline;
}

int w_pipeline_add_filter(w_pipeline* pipeline, w_filter_fn filter) {
    if (pipeline->filter_count >= MAX_FILTERS) {
        return -1;  // Pipeline full
    }
    
    pipeline->filters[pipeline->filter_count++] = filter;
    return 0;
}
```

### Running the Pipeline

```c
int w_pipeline_execute(w_pipeline* pipeline, w_request* req, w_response* resp) {
    for (size_t i = 0; i < pipeline->filter_count; i++) {
        // Execute filter
        int result = pipeline->filters[i](req, resp);
        
        // Check if we should stop
        if (result != 0 || resp->stop_pipeline) {
            return result;
        }
    }
    
    return 0;  // All filters completed successfully
}
```

---

## Integration with State Machine

### Modified Client State Machine

Instead of processing logic in each state, delegate to pipeline:

```c
typedef struct w_client {
    w_client_state state;
    int fd;
    
    // Pipeline-related
    w_pipeline* pipeline;
    w_request* request;
    w_response* response;
    
    // ... other fields ...
} w_client;

void w_client_run(mj_scheduler* scheduler, void* ctx) {
    w_client* client = (w_client*)ctx;
    
    switch (client->state) {
        case W_CLIENT_READING:
            // Just read bytes into buffer
            if (read_complete(client)) {
                client->state = W_CLIENT_PROCESSING;
            }
            break;
            
        case W_CLIENT_PROCESSING:
            // Execute the entire pipeline
            int result = w_pipeline_execute(
                client->pipeline,
                client->request,
                client->response
            );
            
            if (result == 0 || client->response->body != NULL) {
                // We have a response (success or error)
                client->state = W_CLIENT_SENDING;
            } else {
                // Fatal error
                client->state = W_CLIENT_DONE;
            }
            break;
            
        case W_CLIENT_SENDING:
            // Just send the response bytes
            if (send_complete(client)) {
                client->state = W_CLIENT_DONE;
            }
            break;
            
        case W_CLIENT_DONE:
            // Cleanup
            mj_scheduler_task_remove_current(scheduler);
            break;
    }
}
```

### Pipeline Setup in Client Creation

```c
mj_task* w_client_create(int client_fd) {
    // ... allocate task and context ...
    
    // Create processing pipeline
    w_pipeline* pipeline = w_pipeline_create();
    
    // Add filters in order
    w_pipeline_add_filter(pipeline, filter_log_request);
    w_pipeline_add_filter(pipeline, filter_parse_http);
    w_pipeline_add_filter(pipeline, filter_validate_request);
    w_pipeline_add_filter(pipeline, filter_router);
    w_pipeline_add_filter(pipeline, filter_format_http);
    w_pipeline_add_filter(pipeline, filter_log_response);
    
    new_ctx->pipeline = pipeline;
    
    // ... rest of initialization ...
}
```

---

## Benefits of This Approach

### 1. Modularity
Each filter is independent and can be developed/tested separately:

```c
// Unit test for validator
void test_validator(void) {
    w_request req = {.method = "DELETE", .path = "/test"};
    w_response resp = {0};
    
    int result = filter_validate_request(&req, &resp);
    
    assert(result != 0);
    assert(resp.status_code == 405);
    assert(resp.stop_pipeline == 1);
}
```

### 2. Reusability
Filters can be shared across different pipelines:

```c
// HTTP pipeline
w_pipeline_add_filter(http_pipeline, filter_parse_http);
w_pipeline_add_filter(http_pipeline, filter_router);

// WebSocket pipeline (reuses parser)
w_pipeline_add_filter(ws_pipeline, filter_parse_http);
w_pipeline_add_filter(ws_pipeline, filter_upgrade_websocket);
```

### 3. Configurability
Easy to add/remove/reorder filters:

```c
// Development: Add debug filter
if (debug_mode) {
    w_pipeline_add_filter(pipeline, filter_debug_dump);
}

// Production: Add security filters
w_pipeline_add_filter(pipeline, filter_rate_limit);
w_pipeline_add_filter(pipeline, filter_auth_check);
```

### 4. Testability
Can test entire pipeline or individual stages:

```c
// Test full pipeline
w_request req = create_test_request("GET /weather?city=Stockholm");
w_response resp = {0};
w_pipeline_execute(pipeline, &req, &resp);
assert(resp.status_code == 200);

// Test single filter
filter_validate_request(&req, &resp);
```

### 5. Error Handling
Centralized error handling in pipeline executor:

```c
int w_pipeline_execute(w_pipeline* pipeline, w_request* req, w_response* resp) {
    for (size_t i = 0; i < pipeline->filter_count; i++) {
        int result = pipeline->filters[i](req, resp);
        
        if (result != 0) {
            // Log which filter failed
            fprintf(stderr, "Filter %zu failed with code %d\n", i, result);
            
            // Ensure we have an error response
            if (resp->status_code == 0) {
                resp->status_code = 500;
                resp->body = "Internal Server Error";
            }
            return result;
        }
    }
    return 0;
}
```

---

## Advanced Patterns

### Conditional Filters

Some filters might only apply in certain conditions:

```c
int filter_cors(w_request* req, w_response* resp) {
    // Only add CORS headers for browser requests
    const char* origin = get_header(req->http_request, "Origin");
    if (origin) {
        add_header(resp, "Access-Control-Allow-Origin", "*");
    }
    return 0;
}
```

### Filter Context/Configuration

Filters can have configuration:

```c
typedef struct {
    w_filter_fn func;
    void* config;
} w_filter_with_config;

typedef struct {
    int max_requests_per_minute;
    HashTable* ip_counters;
} rate_limit_config;

int filter_rate_limit(w_request* req, w_response* resp, void* config) {
    rate_limit_config* cfg = (rate_limit_config*)config;
    
    // Check rate limit using config
    int count = get_request_count(cfg->ip_counters, req->client_ip);
    if (count > cfg->max_requests_per_minute) {
        resp->status_code = 429;  // Too Many Requests
        resp->body = "Rate limit exceeded";
        resp->stop_pipeline = 1;
        return -1;
    }
    
    return 0;
}
```

### Bidirectional Filters

Some filters need to run both before and after processing:

```c
typedef struct {
    w_filter_fn before;
    w_filter_fn after;
} w_bidirectional_filter;

// Transaction filter
int filter_transaction_begin(w_request* req, w_response* resp) {
    db_begin_transaction();
    return 0;
}

int filter_transaction_end(w_request* req, w_response* resp) {
    if (resp->status_code < 400) {
        db_commit_transaction();
    } else {
        db_rollback_transaction();
    }
    return 0;
}
```

---

## Migration Strategy

### Phase 1: Create Infrastructure
1. Define `w_request` and `w_response` structs
2. Implement `w_pipeline` structure and execution
3. Write helper functions (add_filter, execute, etc.)

### Phase 2: Extract First Filter
1. Start with simplest filter (e.g., logging)
2. Test it works in isolation
3. Integrate into existing state machine

### Phase 3: Progressive Extraction
1. Extract parsing logic → `filter_parse_http`
2. Extract validation → `filter_validate_request`
3. Extract routing → `filter_router`
4. Extract formatting → `filter_format_http`

### Phase 4: Simplify State Machine
1. Remove logic from state machine
2. State machine only handles I/O and calls pipeline
3. Clean up old code

---

## File Organization

Suggested structure:

```
src/
  pipeline/
    pipeline.h          # Core pipeline structures
    pipeline.c          # Pipeline execution engine
    
  filters/
    filter_parser.c     # HTTP parsing filter
    filter_validator.c  # Request validation
    filter_router.c     # URL routing
    filter_formatter.c  # Response formatting
    filter_logger.c     # Request/response logging
    filter_cors.c       # CORS headers
    filter_auth.c       # Authentication
    
  w_client.c           # Simplified state machine
  w_server.c           # Server setup
```

---

## Conclusion

The **Pipes and Filters** architecture transforms HTTP processing from a monolithic state machine into a series of composable, testable stages. Each filter has a single responsibility, making the code easier to understand, test, and maintain.

**Key Takeaways:**
- **Separation of Concerns**: Each filter does one thing
- **Composability**: Filters can be mixed and matched
- **Testability**: Each filter can be unit tested
- **Flexibility**: Easy to add/remove processing steps
- **Maintainability**: Changes are localized to specific filters

**Next Steps:**
1. Implement basic pipeline infrastructure
2. Create simple test filters
3. Gradually migrate existing logic into filters
4. Add new filters as features are needed

This architectural pattern is proven in production systems (Unix pipes, Express.js middleware, Servlet filters) and will significantly improve the quality and maintainability of the UB-WeatherServer codebase.

---

**End of Document**

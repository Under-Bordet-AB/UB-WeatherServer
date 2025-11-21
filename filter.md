# Web Server Filter Pipeline Architecture

## Overview

This document describes the filter pipeline architecture for the web server. Filters are modular components that process requests and responses at specific phases of the request lifecycle.

## Philosophy

**Filters are designed to integrate seamlessly with the state machine, not break it.**

The key insight: filters are just additional states in your state machine. When you're ready to add filters, you simply insert new filter states between existing states. The core logic remains unchanged.

## Filter Phases

Filters execute at five distinct phases:

```
Request Flow:
READING_HEADERS → PARSING_HEADERS → [FILTER_REQUEST] → READING_BODY → 
VALIDATING → [FILTER_POST_AUTH] → ROUTING → [FILTER_PRE_HANDLER] → 
EXECUTING → ASYNC_IO → [FILTER_RESPONSE] → SENDING → [FILTER_POST_SEND] → 
KEEPALIVE/DONE
```

### Phase Descriptions

1. **FILTER_PHASE_REQUEST** - After parsing headers, before reading body
   - Use for: CORS, rate limiting, early request validation
   - Can reject requests before reading large bodies

2. **FILTER_PHASE_POST_AUTH** - After authentication/validation
   - Use for: Authorization checks, request rewriting
   - Has access to authenticated user context

3. **FILTER_PHASE_PRE_HANDLER** - Just before handler execution
   - Use for: Cache lookups, request preprocessing
   - Can provide response and skip handler entirely

4. **FILTER_PHASE_RESPONSE** - After handler, before sending
   - Use for: Compression, cache storage, response transformation
   - Can modify response headers and body

5. **FILTER_PHASE_POST_SEND** - After response sent (fire-and-forget)
   - Use for: Logging, metrics, analytics
   - Cannot affect response

## Core Data Structures

```c
// Filter result codes
typedef enum {
    FILTER_CONTINUE,    // Continue to next filter
    FILTER_DONE,        // Skip remaining filters, proceed to next state
    FILTER_REJECT,      // Stop processing, return error
    FILTER_ASYNC        // Filter needs async I/O, pause pipeline
} filter_result_t;

// Filter phases
typedef enum {
    FILTER_PHASE_REQUEST,
    FILTER_PHASE_POST_AUTH,
    FILTER_PHASE_PRE_HANDLER,
    FILTER_PHASE_RESPONSE,
    FILTER_PHASE_POST_SEND
} filter_phase_t;

// Filter structure
typedef struct filter {
    const char *name;
    filter_phase_t phase;
    int priority;  // Lower number = runs first
    
    // Main execution function
    filter_result_t (*execute)(
        struct filter *self,
        client_ctx_t *client,
        void *context
    );
    
    // Optional: async completion callback
    filter_result_t (*on_async_complete)(
        struct filter *self,
        client_ctx_t *client,
        void *async_result
    );
    
    // Optional: cleanup on error/timeout
    void (*cleanup)(struct filter *self, client_ctx_t *client);
    
    // Filter-specific configuration
    void *config;
    
    // Linked list
    struct filter *next;
} filter_t;

// Pipeline structure (one chain per phase)
typedef struct {
    filter_t *filters[5];  // One chain per phase
    size_t count[5];
} filter_pipeline_t;
```

## Adding Filters to Client Context

```c
typedef struct client_ctx {
    // ... existing fields ...
    
    // Pipeline state
    filter_t *current_filter;      // Current filter (for async resume)
    void *filter_context;          // Filter-specific context data
    bool filter_async_pending;     // Waiting for async operation
    
} client_ctx_t;
```

## Pipeline Management

### Registering Filters

```c
// Global pipeline
static filter_pipeline_t g_pipeline;

void pipeline_register_filter(filter_t *filter) {
    filter_phase_t phase = filter->phase;
    
    // Insert sorted by priority
    filter_t **chain = &g_pipeline.filters[phase];
    
    while (*chain && (*chain)->priority < filter->priority) {
        chain = &(*chain)->next;
    }
    
    filter->next = *chain;
    *chain = filter;
    g_pipeline.count[phase]++;
}
```

### Executing Pipeline

```c
filter_result_t pipeline_execute(client_ctx_t *client, filter_phase_t phase) {
    filter_t *filter = g_pipeline.filters[phase];
    
    // Resume from saved position if async operation completed
    if (client->current_filter) {
        filter = client->current_filter;
        client->current_filter = NULL;
    }
    
    while (filter) {
        filter_result_t result = filter->execute(filter, client, client->filter_context);
        
        switch (result) {
            case FILTER_CONTINUE:
                filter = filter->next;
                break;
                
            case FILTER_DONE:
                return FILTER_DONE;
                
            case FILTER_REJECT:
                return FILTER_REJECT;
                
            case FILTER_ASYNC:
                client->current_filter = filter;
                client->filter_async_pending = true;
                return FILTER_ASYNC;
        }
    }
    
    return FILTER_DONE;
}
```

## State Machine Integration

### Adding Filter States

When you're ready to add filters, simply add these new states:

```c
typedef enum {
    STATE_READING_HEADERS,
    STATE_PARSING_HEADERS,
    STATE_FILTER_REQUEST,      // ← NEW
    STATE_READING_BODY,
    STATE_VALIDATING,
    STATE_FILTER_POST_AUTH,    // ← NEW
    STATE_ROUTING,
    STATE_FILTER_PRE_HANDLER,  // ← NEW
    STATE_EXECUTING,
    STATE_ASYNC_IO,
    STATE_FILTER_RESPONSE,     // ← NEW
    STATE_SENDING,
    STATE_FILTER_POST_SEND,    // ← NEW
    STATE_KEEPALIVE,
    STATE_DONE,
    STATE_ERROR
} client_state_t;
```

### Filter State Implementations

```c
client_state_t w_client_handle_filter_request(client_ctx_t *client) {
    filter_result_t result = pipeline_execute(client, FILTER_PHASE_REQUEST);
    
    switch (result) {
        case FILTER_DONE:
        case FILTER_CONTINUE:
            return http_has_body(client->request) ? 
                STATE_READING_BODY : STATE_VALIDATING;
            
        case FILTER_REJECT:
            return STATE_ERROR;
            
        case FILTER_ASYNC:
            return STATE_FILTER_REQUEST;  // Stay here, waiting
    }
}

client_state_t w_client_handle_filter_post_auth(client_ctx_t *client) {
    filter_result_t result = pipeline_execute(client, FILTER_PHASE_POST_AUTH);
    
    switch (result) {
        case FILTER_DONE:
        case FILTER_CONTINUE:
            return STATE_ROUTING;
        case FILTER_REJECT:
            return STATE_ERROR;
        case FILTER_ASYNC:
            return STATE_FILTER_POST_AUTH;
    }
}

client_state_t w_client_handle_filter_pre_handler(client_ctx_t *client) {
    filter_result_t result = pipeline_execute(client, FILTER_PHASE_PRE_HANDLER);
    
    switch (result) {
        case FILTER_DONE:
            // Filter provided response (cache hit)
            return client->response ? STATE_FILTER_RESPONSE : STATE_EXECUTING;
        case FILTER_CONTINUE:
            return STATE_EXECUTING;
        case FILTER_REJECT:
            return STATE_ERROR;
        case FILTER_ASYNC:
            return STATE_FILTER_PRE_HANDLER;
    }
}

client_state_t w_client_handle_filter_response(client_ctx_t *client) {
    filter_result_t result = pipeline_execute(client, FILTER_PHASE_RESPONSE);
    
    switch (result) {
        case FILTER_DONE:
        case FILTER_CONTINUE:
            return STATE_SENDING;
        case FILTER_REJECT:
            return STATE_ERROR;
        case FILTER_ASYNC:
            return STATE_FILTER_RESPONSE;
    }
}

client_state_t w_client_handle_filter_post_send(client_ctx_t *client) {
    pipeline_execute(client, FILTER_PHASE_POST_SEND);
    
    // Always proceed (post-send is fire-and-forget)
    return client->keep_alive ? STATE_KEEPALIVE : STATE_DONE;
}
```

### Updating Existing States

Modify existing states to transition to filter states:

```c
// PARSING_HEADERS: add filter before body read
client_state_t w_client_handle_parsing_headers(client_ctx_t *client) {
    // ... existing parsing code ...
    return STATE_FILTER_REQUEST;  // Changed from STATE_READING_BODY
}

// VALIDATING: add filter after validation
client_state_t w_client_handle_validating(client_ctx_t *client) {
    // ... existing validation code ...
    return STATE_FILTER_POST_AUTH;  // Changed from STATE_ROUTING
}

// ROUTING: add filter before handler
client_state_t w_client_handle_routing(client_ctx_t *client) {
    // ... existing routing code ...
    return STATE_FILTER_PRE_HANDLER;  // Changed from STATE_EXECUTING
}

// EXECUTING: add filter after handler
client_state_t w_client_handle_executing(client_ctx_t *client) {
    // ... existing execution code ...
    return STATE_FILTER_RESPONSE;  // Changed from STATE_SENDING
}

// SENDING: add filter after send
client_state_t w_client_handle_sending(client_ctx_t *client) {
    // ... existing sending code ...
    if (client->response_sent >= client->response->total_size) {
        return STATE_FILTER_POST_SEND;  // Changed from KEEPALIVE/DONE
    }
    return STATE_SENDING;
}
```

## Example Filters

### 1. Rate Limiting Filter

```c
typedef struct {
    int requests_per_minute;
    hashtable_t *ip_counters;
} rate_limit_config_t;

filter_result_t filter_rate_limit_execute(filter_t *self, client_ctx_t *client, void *ctx) {
    rate_limit_config_t *config = (rate_limit_config_t *)self->config;
    
    char client_ip[INET6_ADDRSTRLEN];
    get_client_ip(client->fd, client_ip, sizeof(client_ip));
    
    rate_counter_t *counter = hashtable_get(config->ip_counters, client_ip);
    if (!counter) {
        counter = rate_counter_create();
        hashtable_set(config->ip_counters, client_ip, counter);
    }
    
    if (!rate_counter_check(counter, config->requests_per_minute)) {
        client->error_code = 429;
        http_set_header(client->request, "Retry-After", "60");
        return FILTER_REJECT;
    }
    
    return FILTER_CONTINUE;
}

static filter_t filter_rate_limit = {
    .name = "rate_limit",
    .phase = FILTER_PHASE_REQUEST,
    .priority = 5,
    .execute = filter_rate_limit_execute,
    .config = &(rate_limit_config_t){
        .requests_per_minute = 60,
        .ip_counters = NULL  // Initialize in pipeline_init()
    }
};
```

### 2. CORS Filter

```c
typedef struct {
    char **allowed_origins;
    size_t origin_count;
    bool allow_credentials;
} cors_config_t;

filter_result_t filter_cors_execute(filter_t *self, client_ctx_t *client, void *ctx) {
    cors_config_t *config = (cors_config_t *)self->config;
    
    const char *origin = http_get_header(client->request, "Origin");
    if (!origin) {
        return FILTER_CONTINUE;
    }
    
    bool allowed = false;
    for (size_t i = 0; i < config->origin_count; i++) {
        if (strcmp(origin, config->allowed_origins[i]) == 0) {
            allowed = true;
            break;
        }
    }
    
    if (!allowed) {
        client->error_code = 403;
        return FILTER_REJECT;
    }
    
    // Save origin for response headers
    http_set_header(client->request, "X-CORS-Origin", origin);
    
    return FILTER_CONTINUE;
}

static filter_t filter_cors = {
    .name = "cors",
    .phase = FILTER_PHASE_REQUEST,
    .priority = 10,
    .execute = filter_cors_execute,
    .config = &(cors_config_t){
        .allowed_origins = (char *[]){"https://example.com"},
        .origin_count = 1,
        .allow_credentials = true
    }
};
```

### 3. Compression Filter

```c
filter_result_t filter_compression_execute(filter_t *self, client_ctx_t *client, void *ctx) {
    const char *accept = http_get_header(client->request, "Accept-Encoding");
    if (!accept || !strstr(accept, "gzip")) {
        return FILTER_CONTINUE;
    }
    
    const char *type = http_get_response_header(client->response, "Content-Type");
    if (!type || !strstr(type, "text/")) {
        return FILTER_CONTINUE;
    }
    
    size_t compressed_size;
    char *compressed = gzip_compress(
        client->response->body,
        client->response->body_size,
        &compressed_size
    );
    
    if (compressed) {
        free(client->response->body);
        client->response->body = compressed;
        client->response->body_size = compressed_size;
        
        http_set_response_header(client->response, "Content-Encoding", "gzip");
        char size_buf[32];
        snprintf(size_buf, sizeof(size_buf), "%zu", compressed_size);
        http_set_response_header(client->response, "Content-Length", size_buf);
    }
    
    return FILTER_CONTINUE;
}

static filter_t filter_compression = {
    .name = "compression",
    .phase = FILTER_PHASE_RESPONSE,
    .priority = 10,
    .execute = filter_compression_execute
};
```

### 4. Cache Filter (with Async)

```c
typedef struct {
    redis_client_t *redis;
} cache_config_t;

typedef struct {
    redis_query_t *query;
} cache_context_t;

filter_result_t filter_cache_execute(filter_t *self, client_ctx_t *client, void *ctx) {
    cache_config_t *config = (cache_config_t *)self->config;
    
    if (strcmp(client->request->method, "GET") != 0) {
        return FILTER_CONTINUE;
    }
    
    char cache_key[256];
    snprintf(cache_key, sizeof(cache_key), "cache:%s", client->request->uri);
    
    cache_context_t *cache_ctx = malloc(sizeof(cache_context_t));
    cache_ctx->query = redis_get_async(config->redis, cache_key);
    client->filter_context = cache_ctx;
    
    return FILTER_ASYNC;
}

filter_result_t filter_cache_on_async_complete(
    filter_t *self, 
    client_ctx_t *client, 
    void *result
) {
    cache_context_t *cache_ctx = (cache_context_t *)client->filter_context;
    redis_result_t *redis_result = (redis_result_t *)result;
    
    if (redis_result->found) {
        // Cache hit!
        client->response = http_response_from_cached(
            redis_result->data, 
            redis_result->size
        );
        free(cache_ctx);
        client->filter_context = NULL;
        return FILTER_DONE;  // Skip handler
    }
    
    // Cache miss
    free(cache_ctx);
    client->filter_context = NULL;
    return FILTER_CONTINUE;
}

void filter_cache_cleanup(filter_t *self, client_ctx_t *client) {
    if (client->filter_context) {
        cache_context_t *ctx = (cache_context_t *)client->filter_context;
        redis_cancel_query(ctx->query);
        free(ctx);
        client->filter_context = NULL;
    }
}

static filter_t filter_cache_lookup = {
    .name = "cache_lookup",
    .phase = FILTER_PHASE_PRE_HANDLER,
    .priority = 5,
    .execute = filter_cache_execute,
    .on_async_complete = filter_cache_on_async_complete,
    .cleanup = filter_cache_cleanup
};
```

### 5. Security Headers Filter

```c
filter_result_t filter_security_headers_execute(
    filter_t *self, 
    client_ctx_t *client, 
    void *ctx
) {
    http_set_response_header(client->response, 
        "X-Content-Type-Options", "nosniff");
    http_set_response_header(client->response, 
        "X-Frame-Options", "DENY");
    http_set_response_header(client->response, 
        "X-XSS-Protection", "1; mode=block");
    http_set_response_header(client->response, 
        "Strict-Transport-Security", "max-age=31536000");
    
    return FILTER_CONTINUE;
}

static filter_t filter_security_headers = {
    .name = "security_headers",
    .phase = FILTER_PHASE_RESPONSE,
    .priority = 50,
    .execute = filter_security_headers_execute
};
```

### 6. Logging Filters

```c
// Request logging
filter_result_t filter_request_logger_execute(
    filter_t *self, 
    client_ctx_t *client, 
    void *ctx
) {
    log_info("Request: %s %s from %s",
        client->request->method,
        client->request->uri,
        client->request->client_ip);
    
    client->request->start_time = get_monotonic_time();
    return FILTER_CONTINUE;
}

static filter_t filter_request_logger = {
    .name = "request_logger",
    .phase = FILTER_PHASE_REQUEST,
    .priority = 1,
    .execute = filter_request_logger_execute
};

// Response logging
filter_result_t filter_response_logger_execute(
    filter_t *self, 
    client_ctx_t *client, 
    void *ctx
) {
    double elapsed = get_monotonic_time() - client->request->start_time;
    
    log_info("Response: %d %s %.3fms",
        client->response->status_code,
        client->request->uri,
        elapsed * 1000.0);
    
    return FILTER_CONTINUE;
}

static filter_t filter_response_logger = {
    .name = "response_logger",
    .phase = FILTER_PHASE_POST_SEND,
    .priority = 100,
    .execute = filter_response_logger_execute
};
```

## Initialization

```c
void pipeline_init(void) {
    memset(&g_pipeline, 0, sizeof(g_pipeline));
    
    // Request phase
    pipeline_register_filter(&filter_request_logger);
    pipeline_register_filter(&filter_rate_limit);
    pipeline_register_filter(&filter_cors);
    
    // Pre-handler phase
    pipeline_register_filter(&filter_cache_lookup);
    
    // Response phase
    pipeline_register_filter(&filter_compression);
    pipeline_register_filter(&filter_security_headers);
    
    // Post-send phase
    pipeline_register_filter(&filter_response_logger);
}
```

## Migration Strategy

### Starting Without Filters

If you start with the basic state machine without filters, here's how to add them later:

1. **Add filter states to enum** (no code changes yet)
2. **Add filter state handler functions** (they can just return next state initially)
3. **Update state transitions** to go through filter states
4. **Implement pipeline_execute()** function
5. **Add your first filter** and register it
6. **Test thoroughly** with and without filters enabled

### Minimal Working Example

```c
// Start with this minimal filter that does nothing
client_state_t w_client_handle_filter_request(client_ctx_t *client) {
    // TODO: Add pipeline_execute() later
    return http_has_body(client->request) ? 
        STATE_READING_BODY : STATE_VALIDATING;
}
```

Then update PARSING_HEADERS to transition to it:

```c
return STATE_FILTER_REQUEST;  // Instead of STATE_READING_BODY
```

Your state machine works exactly the same, but now has hooks for filters!

## Best Practices

1. **Keep filters focused** - Each filter should do one thing
2. **Use priority carefully** - Lower numbers run first
3. **Document dependencies** - If filter B needs filter A, use priorities
4. **Handle cleanup** - Always implement cleanup for async filters
5. **Fail safely** - Return FILTER_CONTINUE on non-critical failures
6. **Test independently** - Each filter should be unit-testable
7. **Log filter actions** - For debugging and monitoring
8. **Make filters optional** - Use feature flags to enable/disable
9. **Measure performance** - Track time spent in each filter
10. **Version filter config** - Make config changes backward-compatible

## Common Patterns

### Early Rejection Pattern

```c
if (condition_not_met) {
    client->error_code = 400;
    return FILTER_REJECT;
}
return FILTER_CONTINUE;
```

### Async Operation Pattern

```c
if (!async_operation_started) {
    context = start_async_operation();
    client->filter_context = context;
    return FILTER_ASYNC;
}
```

### Short-Circuit Pattern

```c
if (response_ready) {
    client->response = create_response();
    return FILTER_DONE;  // Skip remaining filters
}
return FILTER_CONTINUE;
```

### Transform Pattern

```c
transform_request_or_response(client);
return FILTER_CONTINUE;
```

## Debugging

Enable filter tracing:

```c
filter_result_t result = filter->execute(filter, client, ctx);
log_debug("Filter '%s' returned %s", 
    filter->name, 
    filter_result_to_string(result));
```

## Performance Considerations

- **Minimize allocations** in hot-path filters
- **Cache lookups** for repeated header checks
- **Batch operations** where possible
- **Use priority** to run cheap filters first
- **Profile** filter execution time
- **Consider async** for any I/O operation

## Future Enhancements

- **Filter groups** - Enable/disable sets of filters
- **Conditional filters** - Run filters based on route/condition
- **Filter stats** - Track execution count, time, failures
- **Hot reload** - Update filter config without restart
- **Filter chains** - Nest filters within filters
- **Per-route filters** - Different filters for different endpoints

---

**Remember**: Filters integrate cleanly into your state machine. They don't break anything - they're just new states you add when ready!
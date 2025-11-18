# Pipeline Pattern Explained

**Date:** November 18, 2025  
**Project:** UB-WeatherServer

---

## Table of Contents
1. [What is the Pipeline Pattern?](#what-is-the-pipeline-pattern)
2. [Your Current Architecture](#your-current-architecture)
3. [Visual Comparison](#visual-comparison)
4. [Benefits of Pipeline Pattern](#benefits-of-pipeline-pattern)
5. [Drawbacks of Pipeline Pattern](#drawbacks-of-pipeline-pattern)
6. [When to Use Pipelines](#when-to-use-pipelines)
7. [Hybrid Approach](#hybrid-approach-for-your-server)
8. [Real-World Examples](#real-world-example-web-frameworks)
9. [Recommendation](#recommendation-for-your-server)

---

## What is the Pipeline Pattern?

The **Pipeline pattern** (also called **Pipes and Filters**) is a software design pattern where data flows through a series of processing stages (filters), with each stage performing a specific transformation. Think of it like an assembly line in a factory - each station does one job, then passes the work to the next station.

```
Input → Filter 1 → Filter 2 → Filter 3 → Filter 4 → Output
        (Parse)   (Validate)  (Route)    (Format)
```

### Key Concepts

**Filter:** A processing unit that:
- Receives input data
- Performs ONE specific transformation
- Outputs transformed data
- Is independent of other filters

**Pipe:** The connection between filters that passes data along

**Pipeline:** The entire chain of filters working together

### Simple Example
```c
// Each filter is a function with standard signature
typedef int (*w_filter_fn)(w_request* req, w_response* resp);

// Pipeline structure
typedef struct w_pipeline {
    w_filter_fn filters[MAX_FILTERS];
    size_t filter_count;
} w_pipeline;

// Execute all filters in sequence
int w_pipeline_run(w_pipeline* pipeline, w_request* req, w_response* resp) {
    for (size_t i = 0; i < pipeline->filter_count; i++) {
        int result = pipeline->filters[i](req, resp);
        if (result != 0) {
            return result;  // Stop on error
        }
    }
    return 0;
}
```

---

## Your Current Architecture

Your current code uses a **state machine** approach in `w_client.c`:

```c
void w_client_run(mj_scheduler* scheduler, void* ctx) {
    w_client* client = (w_client*)ctx;
    
    switch (client->state) {
        case W_CLIENT_READING:
            // Read bytes from socket
            // Parse HTTP headers
            // Validate content length
            // Check for complete request
            // Route to handler
            // Execute business logic
            // Format response
            // ALL MIXED TOGETHER
            break;
            
        case W_CLIENT_PARSING:
            // More processing...
            break;
            
        case W_CLIENT_PROCESSING:
            // More processing...
            break;
            
        case W_CLIENT_SENDING:
            // Send response
            break;
    }
}
```

This is **monolithic** - all HTTP processing logic is embedded within the state machine cases. One function (or a few tightly coupled functions) does many jobs:

1. Read from socket
2. Parse HTTP headers
3. Validate request
4. Route to handler
5. Generate response
6. Format output

All in sequential, tightly coupled code.

---

## Visual Comparison

### Your Current Architecture: Monolithic State Machine

```
┌─────────────────────────────────────────────────────┐
│     w_client_run() - State Machine                  │
│                                                      │
│  ┌────────────────────────────────────────────┐    │
│  │  READING State                             │    │
│  │  ┌──────────────────────────────────────┐  │    │
│  │  │ Read socket                          │  │    │
│  │  │ Parse HTTP                           │  │    │
│  │  │ Validate request                     │  │    │
│  │  │ Check headers                        │  │    │
│  │  │ All logic mixed together             │  │    │
│  │  └──────────────────────────────────────┘  │    │
│  └────────────────────────────────────────────┘    │
│                                                      │
│  ┌────────────────────────────────────────────┐    │
│  │  PROCESSING State                          │    │
│  │  ┌──────────────────────────────────────┐  │    │
│  │  │ Route to handler                     │  │    │
│  │  │ Execute handler                      │  │    │
│  │  │ Format response                      │  │    │
│  │  │ More mixed logic                     │  │    │
│  │  └──────────────────────────────────────┘  │    │
│  └────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────┘
```

**Characteristics:**
- Everything happens inside one large function/switch statement
- Logic for parsing, validation, routing all intermingled
- Hard to isolate individual concerns
- Testing requires running the entire state machine

---

### Pipeline Architecture: Modular Filters

```
                    Request Flow →
                    
┌────────┐   ┌──────────┐   ┌──────────┐   ┌────────┐   ┌──────────┐
│ Parse  │──→│ Validate │──→│  Route   │──→│ Handle │──→│  Format  │
│  HTTP  │   │ Request  │   │ Request  │   │ Logic  │   │ Response │
└────────┘   └──────────┘   └──────────┘   └────────┘   └──────────┘
    ↑                                                            ↓
    │                                                            │
    └───────────────── w_request / w_response ──────────────────┘
```

**Characteristics:**
- Each box is an independent filter function
- Clear, linear data flow
- Filters can be tested independently
- Easy to add/remove/reorder filters

---

### Side-by-Side Code Comparison

#### Current Approach (Monolithic)
```c
void w_client_run(mj_scheduler* scheduler, void* ctx) {
    w_client* client = (w_client*)ctx;
    
    switch (client->state) {
        case W_CLIENT_READING:
            // Read data
            ssize_t n = recv(client->fd, buffer, size, 0);
            
            // Parse inline
            HTTPRequest* req = HTTPRequest_fromstring(buffer);
            
            // Validate inline
            if (strlen(req->URL) > MAX_URL) {
                // error handling...
            }
            
            // Route inline
            if (strcmp(req->URL, "/weather") == 0) {
                // handle weather...
            }
            
            // Format inline
            HTTPResponse* resp = HTTPResponse_new(200, body);
            
            client->state = W_CLIENT_SENDING;
            break;
    }
}
```

#### Pipeline Approach (Modular)
```c
// Filter 1: Parse
int filter_parse(w_request* req, w_response* resp) {
    req->http_request = HTTPRequest_fromstring(req->raw_data);
    return req->http_request ? 0 : -1;
}

// Filter 2: Validate
int filter_validate(w_request* req, w_response* resp) {
    if (strlen(req->path) > MAX_URL) {
        resp->status_code = 414;
        return -1;
    }
    return 0;
}

// Filter 3: Route
int filter_route(w_request* req, w_response* resp) {
    if (strcmp(req->path, "/weather") == 0) {
        return handle_weather(req, resp);
    }
    resp->status_code = 404;
    return -1;
}

// Execute pipeline
void w_client_run(mj_scheduler* scheduler, void* ctx) {
    w_client* client = (w_client*)ctx;
    
    switch (client->state) {
        case W_CLIENT_PROCESSING:
            // Just run the pipeline
            w_pipeline_run(client->pipeline, client->request, client->response);
            client->state = W_CLIENT_SENDING;
            break;
    }
}
```

---

## Benefits of Pipeline Pattern

### ✅ 1. Single Responsibility Principle

Each filter does **ONE thing** and does it well.

**Example:**
```c
// Parser filter ONLY parses - nothing else
int filter_parse_http(w_request* req, w_response* resp) {
    req->http_request = HTTPRequest_fromstring(req->raw_data);
    req->method = RequestMethod_tostring(req->http_request->method);
    req->path = req->http_request->URL;
    return req->http_request->valid ? 0 : -1;
}

// Validator filter ONLY validates - nothing else
int filter_validate_request(w_request* req, w_response* resp) {
    if (strlen(req->path) > MAX_URL_LEN) {
        resp->status_code = 414;
        resp->body = "URI Too Long";
        return -1;
    }
    return 0;
}
```

**Your current code:** `w_client_run()` does everything - parsing, validating, routing, formatting all mixed together.

**Benefit:** Easier to understand, modify, and debug when each piece has one clear purpose.

---

### ✅ 2. Easy to Test

Each filter can be unit tested independently:

```c
// Test parser in isolation
void test_parser(void) {
    w_request req = {.raw_data = "GET /test HTTP/1.1\r\n\r\n"};
    w_response resp = {0};
    
    int result = filter_parse_http(&req, &resp);
    
    assert(result == 0);
    assert(strcmp(req.method, "GET") == 0);
    assert(strcmp(req.path, "/test") == 0);
    printf("✓ Parser test passed\n");
}

// Test validator in isolation
void test_validator(void) {
    w_request req = {.path = "/very/long/url/..."};  // > MAX_URL_LEN
    w_response resp = {0};
    
    int result = filter_validate_request(&req, &resp);
    
    assert(result != 0);
    assert(resp.status_code == 414);
    printf("✓ Validator test passed\n");
}
```

**Your current code:** Must test entire `w_client_run()` state machine at once, making it harder to isolate which part is failing.

**Benefit:** Find bugs faster, test edge cases easily, gain confidence in code quality.

---

### ✅ 3. Reusability

Filters can be shared across different pipelines:

```c
// HTTP pipeline
w_pipeline* http_pipeline = w_pipeline_create();
w_pipeline_add(http_pipeline, filter_parse_http);
w_pipeline_add(http_pipeline, filter_validate_request);
w_pipeline_add(http_pipeline, filter_route);

// HTTPS pipeline reuses same filters
w_pipeline* https_pipeline = w_pipeline_create();
w_pipeline_add(https_pipeline, filter_parse_http);      // Reuse!
w_pipeline_add(https_pipeline, filter_validate_request); // Reuse!
w_pipeline_add(https_pipeline, filter_tls_verify);      // New filter
w_pipeline_add(https_pipeline, filter_route);           // Reuse!

// WebSocket pipeline reuses parser
w_pipeline* ws_pipeline = w_pipeline_create();
w_pipeline_add(ws_pipeline, filter_parse_http);         // Reuse!
w_pipeline_add(ws_pipeline, filter_upgrade_websocket);
```

**Your current code:** Logic is embedded in specific functions, hard to extract and reuse elsewhere.

**Benefit:** Write once, use in multiple contexts. Less code duplication.

---

### ✅ 4. Easy to Extend

Want to add a new feature? Just insert a filter:

```c
// Original pipeline
w_pipeline_add(pipeline, filter_parse);
w_pipeline_add(pipeline, filter_validate);
w_pipeline_add(pipeline, filter_route);

// Add logging - just insert a filter
w_pipeline_add(pipeline, filter_parse);
w_pipeline_add(pipeline, filter_log_request);    // NEW!
w_pipeline_add(pipeline, filter_validate);
w_pipeline_add(pipeline, filter_route);

// Add authentication - insert another filter
w_pipeline_add(pipeline, filter_parse);
w_pipeline_add(pipeline, filter_log_request);
w_pipeline_add(pipeline, filter_authenticate);   // NEW!
w_pipeline_add(pipeline, filter_validate);
w_pipeline_add(pipeline, filter_route);
```

**Your current code:** Must modify `w_client_run()` and add calls throughout, risking breaking existing functionality.

**Benefit:** Add features without touching existing code (Open/Closed Principle).

---

### ✅ 5. Clear Data Flow

Pipeline makes the processing flow explicit and obvious:

```
Request → Parse → Validate → Authenticate → Route → Handle → Format → Response
```

Anyone reading the code can immediately understand the sequence of operations.

**Your current code:** Flow is hidden inside function calls, conditionals, and state transitions. Must read through implementation to understand order.

**Benefit:** Easier onboarding for new developers, clearer architecture documentation.

---

### ✅ 6. Easy to Debug

Insert debugging filters anywhere to inspect data at that stage:

```c
int filter_debug_dump(w_request* req, w_response* resp) {
    printf("=== DEBUG CHECKPOINT ===\n");
    printf("Method: %s\n", req->method);
    printf("Path: %s\n", req->path);
    printf("Body length: %zu\n", req->body_len);
    printf("========================\n");
    return 0;  // Continue pipeline
}

// Insert between any stages
w_pipeline_add(pipeline, filter_parse);
w_pipeline_add(pipeline, filter_debug_dump);     // See parsed data
w_pipeline_add(pipeline, filter_validate);
w_pipeline_add(pipeline, filter_debug_dump);     // See validated data
w_pipeline_add(pipeline, filter_route);
```

**Your current code:** Must add `printf` statements scattered throughout functions, harder to see state at specific points.

**Benefit:** Rapid debugging, can enable/disable debug filters conditionally.

---

### ✅ 7. Conditional Processing

Some filters can decide to stop the pipeline early:

```c
int filter_rate_limit(w_request* req, w_response* resp) {
    if (request_count > MAX_REQUESTS) {
        resp->status_code = 429;  // Too Many Requests
        resp->body = "Rate limit exceeded";
        return -1;  // Stop pipeline
    }
    return 0;  // Continue
}

// If rate limit triggers, remaining filters never run
w_pipeline_add(pipeline, filter_rate_limit);   // Might stop here
w_pipeline_add(pipeline, filter_parse);        // Won't execute if rate limited
w_pipeline_add(pipeline, filter_route);        // Won't execute if rate limited
```

**Benefit:** Early exit conditions are clear and localized to specific filters.

---

## Drawbacks of Pipeline Pattern

### ❌ 1. Increased Complexity

**Before (your code):**
```c
// Simple, straightforward
void w_client_run(mj_scheduler* scheduler, void* ctx) {
    w_client* client = (w_client*)ctx;
    // Process request...
}
```

**After (pipeline):**
```c
// Must manage pipeline infrastructure
w_pipeline* pipeline = w_pipeline_create();
w_pipeline_add(pipeline, filter1);
w_pipeline_add(pipeline, filter2);
w_pipeline_add(pipeline, filter3);
w_pipeline_run(pipeline, req, resp);
w_pipeline_destroy(pipeline);

// Plus all the filter functions...
int filter1(w_request* req, w_response* resp) { /* ... */ }
int filter2(w_request* req, w_response* resp) { /* ... */ }
int filter3(w_request* req, w_response* resp) { /* ... */ }
```

More structures, more code, more moving parts to maintain.

**Impact:** Higher learning curve, more boilerplate code.

---

### ❌ 2. Performance Overhead

Each filter is a function call, potentially preventing compiler optimizations:

```c
// Your way: Compiler can inline and optimize aggressively
void process_request(w_client* client) {
    parse(client);     // Might be inlined
    validate(client);  // Might be inlined
    route(client);     // Might be inlined
    // Compiler sees entire flow, can optimize globally
}

// Pipeline way: Indirect function calls through pointers
for (size_t i = 0; i < pipeline->filter_count; i++) {
    pipeline->filters[i](req, resp);  // Indirect call, harder to optimize
}
```

**Measurements:**
- Function call overhead: ~1-5 nanoseconds per call
- Lost inline optimizations: varies
- For I/O-bound servers: **negligible**
- For CPU-bound processing: **measurable**

**Reality:** For a web server spending microseconds on network I/O, the overhead is insignificant. But it's real.

---

### ❌ 3. Data Coupling

All filters must agree on shared data structures:

```c
typedef struct w_request {
    const char* method;
    const char* path;
    const char* body;
    size_t body_len;
    // Every filter sees ALL fields
} w_request;
```

**Problem:** Change one field, might affect multiple filters. Adding a field to `w_request` requires reviewing all filters that touch it.

**Contrast:** In monolithic code, you have local variables that only the function sees.

**Mitigation:** Use opaque pointers and accessor functions:
```c
const char* w_request_get_method(w_request* req);
void w_request_set_method(w_request* req, const char* method);
```

But this adds more complexity...

---

### ❌ 4. Error Handling Complexity

When a filter fails, you need to know which one and why:

```c
int w_pipeline_run(w_pipeline* pipeline, w_request* req, w_response* resp) {
    for (size_t i = 0; i < pipeline->filter_count; i++) {
        int result = pipeline->filters[i](req, resp);
        if (result != 0) {
            // Which filter failed? Filter #3? What does that mean?
            // Need to track filter names, error codes, stack traces...
            fprintf(stderr, "Filter %zu failed with code %d\n", i, result);
            return result;
        }
    }
    return 0;
}
```

**Solution:** Add filter metadata:
```c
typedef struct w_filter {
    const char* name;
    w_filter_fn func;
} w_filter;

// Now you can report: "Filter 'validator' failed with code -1"
```

But again, more complexity...

---

### ❌ 5. Debugging Difficulty

When something goes wrong, you need to trace through multiple function calls:

```
Pipeline failed
  ↓
Which filter?
  ↓
Look at filter function
  ↓
Check input/output
  ↓
Trace back to previous filter
  ↓
...
```

**Your current code:** Everything in one place, easier to step through in a debugger.

**Mitigation:** Good logging, filter names, debug filters. But it takes more effort.

---

### ❌ 6. Overkill for Simple Cases

If your HTTP handling is simple and won't change much:

```c
// Simple processing: just parse and respond
void handle_request(w_client* client) {
    HTTPRequest* req = parse(client->buffer);
    if (strcmp(req->URL, "/health") == 0) {
        send(client->fd, "OK", 2, 0);
    }
}
```

Adding a full pipeline framework is overkill. The overhead and complexity don't pay for themselves.

**Rule of thumb:** If your processing is < 100 lines and stable, pipelines are probably unnecessary.

---

### ❌ 7. Memory Management

Pipelines often require allocating request/response objects that get passed through filters:

```c
w_request* req = w_request_create();
w_response* resp = w_response_create();

w_pipeline_run(pipeline, req, resp);

w_request_destroy(req);
w_response_destroy(resp);
```

**Your current code:** Can use stack allocation and local variables, simpler lifetime management.

**Impact:** More potential for memory leaks if cleanup isn't done properly.

---

## When to Use Pipelines

### ✅ Good Fit: Use Pipelines When...

1. **Complex Processing with Many Stages**
   - HTTP servers with authentication, validation, routing, logging, etc.
   - Image processing (resize → crop → filter → compress → save)
   - Compilers (lex → parse → optimize → codegen)

2. **Need to Reuse Processing Stages**
   - Same validator for HTTP and HTTPS
   - Same parser for multiple protocols
   - Shared logging across different endpoints

3. **Requirements Change Frequently**
   - Adding features regularly (logging, metrics, auth)
   - Experimenting with different processing orders
   - A/B testing different handlers

4. **Multiple Similar Pipelines**
   - HTTP, HTTPS, WebSocket all need similar processing
   - Different API versions with overlapping logic
   - Multiple protocols sharing common filters

5. **Testing Individual Stages is Important**
   - Security-critical validation that needs thorough testing
   - Complex parsing logic that must be verified
   - Business logic that requires unit tests

6. **Clear Separation of Concerns Matters**
   - Large team where different people own different stages
   - Need to understand processing flow quickly
   - Documentation and maintainability are priorities

---

### ❌ Bad Fit: Avoid Pipelines When...

1. **Simple, Linear Processing**
   ```c
   // Just two steps, don't need pipeline
   void handle_request(char* data) {
       parse(data);
       respond();
   }
   ```

2. **Performance is Absolutely Critical**
   - Real-time systems with nanosecond budgets
   - High-frequency trading systems
   - Embedded systems with tight constraints

3. **Only One or Two Processing Steps**
   - A static file server (read file → send)
   - A simple proxy (receive → forward)
   - A health check endpoint (always return "OK")

4. **Requirements are Stable**
   - Code hasn't changed in years and won't
   - Well-defined spec that's frozen
   - No plans to extend functionality

5. **Team is Unfamiliar with Pattern**
   - Learning curve is too steep
   - No time for training
   - Simpler alternatives exist

6. **Prototyping/MVP Stage**
   - Need to move fast
   - Architecture will change significantly
   - Don't know final requirements yet

---

## Hybrid Approach for Your Server

You don't have to choose all-or-nothing. Consider a **hybrid** architecture:

### Keep State Machine for Connection Lifecycle

The state machine is **perfect** for managing connection states:

```c
void w_client_run(mj_scheduler* scheduler, void* ctx) {
    w_client* client = (w_client*)ctx;
    
    switch (client->state) {
        case W_CLIENT_READING:
            // State machine handles I/O
            if (recv_complete(client)) {
                client->state = W_CLIENT_PROCESSING;
            }
            break;
            
        case W_CLIENT_PROCESSING:
            // ← Use pipeline HERE
            break;
            
        case W_CLIENT_SENDING:
            // State machine handles I/O
            if (send_complete(client)) {
                client->state = W_CLIENT_DONE;
            }
            break;
            
        case W_CLIENT_DONE:
            cleanup(client);
            break;
    }
}
```

### Use Pipeline Inside Request Processing

```c
case W_CLIENT_PROCESSING:
    // Create request/response objects
    w_request* req = w_request_from_buffer(client->buffer);
    w_response* resp = w_response_create();
    
    // Run HTTP processing pipeline
    int result = w_pipeline_run(
        server->http_pipeline,
        req,
        resp
    );
    
    if (result == 0 && resp->body != NULL) {
        // Success - prepare to send response
        client->response_data = resp->body;
        client->response_len = resp->body_len;
        client->state = W_CLIENT_SENDING;
    } else {
        // Error - close connection
        client->state = W_CLIENT_DONE;
    }
    break;
```

### Benefits of Hybrid Approach

✅ **State machine for connection flow:**
- Simple and fast
- Clear connection lifecycle
- Easy to understand states (READING → PROCESSING → SENDING)

✅ **Pipeline for HTTP processing:**
- Modular request handling
- Testable business logic
- Extensible for new features

✅ **Best of both worlds:**
- Simplicity where it matters (I/O)
- Flexibility where it matters (logic)

---

## Real-World Example: Web Frameworks

Most successful web frameworks use the pipeline pattern for request processing (often called "middleware"):

### Express.js (Node.js)
```javascript
const express = require('express');
const app = express();

// Each use() adds a filter to the pipeline
app.use(bodyParser.json());          // Parse JSON bodies
app.use(validateRequest());          // Validate request
app.use(authenticate());             // Check authentication
app.use(logger());                   // Log request
app.use(rateLimit());                // Rate limiting

// Route handlers are final filters
app.get('/weather', weatherHandler);
app.get('/health', healthHandler);
```

### ASP.NET Core (C#)
```csharp
public void Configure(IApplicationBuilder app) {
    app.UseRouting();           // Route requests
    app.UseAuthentication();    // Authenticate user
    app.UseAuthorization();     // Check permissions
    app.UseEndpoints(endpoints => {
        endpoints.MapControllers();
    });
}
```

### Django (Python)
```python
MIDDLEWARE = [
    'django.middleware.security.SecurityMiddleware',
    'django.middleware.common.CommonMiddleware',
    'django.middleware.csrf.CsrfViewMiddleware',
    'django.contrib.auth.middleware.AuthenticationMiddleware',
]
```

### Ruby on Rails
```ruby
# Middleware stack
use Rack::Logger
use Rack::Auth::Basic
use Rack::ContentLength
use Rack::Static
```

All these frameworks use **the same pattern**: a series of filters (middleware) that each request passes through.

**Why?** Because it works. It's proven at massive scale (Netflix, Facebook, GitHub all use these frameworks).

---

## Recommendation for Your Server

### Current State: Your Server is Fine

Your current monolithic state machine approach is **perfectly acceptable** for where you are now:
- Server is small and focused
- HTTP handling is incomplete but simple
- Learning and experimentation phase
- Code is manageable at current size

**Don't prematurely optimize architecture.**

---

### Consider Pipeline When You Hit These Triggers

1. **Adding Authentication/Authorization**
   ```c
   // This is a perfect filter candidate
   int filter_check_auth(w_request* req, w_response* resp) {
       if (!has_valid_token(req)) {
           resp->status_code = 401;
           return -1;
       }
       return 0;
   }
   ```

2. **Implementing Rate Limiting**
   ```c
   int filter_rate_limit(w_request* req, w_response* resp) {
       if (exceeded_rate_limit(req->client_ip)) {
           resp->status_code = 429;
           return -1;
       }
       return 0;
   }
   ```

3. **Adding Request Logging**
   ```c
   int filter_log_request(w_request* req, w_response* resp) {
       log_info("%s %s from %s", req->method, req->path, req->client_ip);
       return 0;
   }
   ```

4. **Supporting Multiple Content Types**
   ```c
   int filter_content_negotiation(w_request* req, w_response* resp) {
       const char* accept = get_header(req, "Accept");
       if (strstr(accept, "application/json")) {
           resp->content_type = CT_JSON;
       } else if (strstr(accept, "text/html")) {
           resp->content_type = CT_HTML;
       }
       return 0;
   }
   ```

5. **HTTP Handling Grows Beyond 200-300 Lines**
   - When one function does too much
   - When testing becomes difficult
   - When changes break unexpected things

---

### Start Small: Incremental Refactoring

You don't need a full pipeline framework. Start by extracting obvious stages into separate functions:

#### Phase 1: Extract Functions (No Framework)
```c
// Just separate functions, still called sequentially
void w_client_process(w_client* client) {
    // Parse
    HTTPRequest* req = w_http_parse(client->buffer);
    if (!req) {
        w_send_error(client, 400);
        return;
    }
    
    // Validate
    if (!w_http_validate(req)) {
        w_send_error(client, 400);
        HTTPRequest_Dispose(&req);
        return;
    }
    
    // Route
    HTTPResponse* resp = w_http_route(req);
    
    // Send
    w_http_send(client, resp);
    
    // Cleanup
    HTTPRequest_Dispose(&req);
    HTTPResponse_Dispose(&resp);
}
```

This gives you 80% of the benefits with 20% of the complexity.

#### Phase 2: Add Pipeline When Needed
Only if you need more flexibility later:
```c
void w_client_process(w_client* client) {
    w_request req = w_request_from_client(client);
    w_response resp = {0};
    
    w_pipeline_run(client->pipeline, &req, &resp);
    
    w_send_response(client, &resp);
}
```

---

### Decision Matrix

| Your Situation | Recommendation |
|----------------|----------------|
| HTTP handling < 100 lines | ✅ Keep current approach |
| 100-300 lines | 🟡 Extract functions, no pipeline yet |
| 300-500 lines | 🟡 Consider simple pipeline |
| 500+ lines | 🔴 Definitely use pipeline |
| Adding 3+ cross-cutting concerns | 🔴 Use pipeline |
| One-time prototype | ✅ Keep it simple |
| Long-term production system | 🟡 Plan for pipeline |
| Team of 1-2 developers | ✅ Simple approach fine |
| Team of 5+ developers | 🔴 Use pipeline for clarity |

---

## Summary

### Quick Reference Table

| Aspect | State Machine (Current) | Pipeline Pattern |
|--------|------------------------|------------------|
| **Complexity** | Lower | Higher |
| **Initial Development Time** | Faster | Slower |
| **Modularity** | Mixed together | Separate filters |
| **Testability** | Test everything together | Test filters individually |
| **Performance** | Slightly faster | Slightly slower (~5%) |
| **Extensibility** | Harder to extend | Easy to extend |
| **Learning Curve** | Easier | Steeper |
| **Code Clarity** | Flow hidden in code | Flow explicit in pipeline |
| **Debugging** | Easier (one place) | Harder (multiple places) |
| **Reusability** | Low | High |
| **Best For** | Simple, stable code | Complex, evolving code |
| **Lines of Code** | Fewer | More (but organized) |
| **Memory Usage** | Less | More (request/response objects) |
| **Team Size** | Good for 1-3 | Better for 5+ |

---

### The Bottom Line

**For Your Current Project:**
- ✅ Your state machine approach is **fine for now**
- ✅ Focus on completing core functionality first
- ✅ Don't add complexity prematurely

**Consider Pipelines When:**
- 🔄 HTTP processing becomes hard to manage (>300 lines)
- 🔄 You need to add multiple cross-cutting features (auth, logging, rate limiting)
- 🔄 Testing becomes difficult
- 🔄 You find yourself copying code between similar handlers

**Migration Path:**
1. **Now:** Keep state machine, finish core features
2. **Next:** Extract HTTP processing into separate functions
3. **Later:** Consider pipeline if complexity warrants it

**Remember:** Pipelines are a tool, not a goal. Use them when they solve a problem you actually have, not because they're "better" in the abstract.

---

## Further Reading

### Books
- *Pattern-Oriented Software Architecture, Volume 4* (Pipes and Filters pattern)
- *Design Patterns: Elements of Reusable Object-Oriented Software* (Chain of Responsibility - similar concept)

### Real-World Implementations
- UNIX shell pipelines (`cat file.txt | grep "error" | wc -l`)
- Servlet filters (Java web applications)
- Express.js middleware (Node.js)
- ASP.NET Core middleware (C#)
- Django middleware (Python)

### Articles
- Martin Fowler's "Pipes and Filters" pattern
- Microsoft's "Pipeline pattern" documentation
- "Understanding Middleware" from Express.js docs

---

**End of Document**

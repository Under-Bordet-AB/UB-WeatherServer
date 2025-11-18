# Code Review: UB-WeatherServer Architecture

**Review Date:** November 18, 2025  
**Branch:** feature/new-scheduler  
**Reviewer:** AI Code Review

---

## Executive Summary

This is a custom C-based HTTP server built around a cooperative task scheduler (`majjen`). The architecture demonstrates a clean separation of concerns with a fixed-size task scheduler managing both server listening and client connections. The codebase is in active development with incomplete HTTP handling but shows solid foundational design choices.

**Overall Assessment:** The architecture is sound for a lightweight cooperative server, but several critical features are incomplete and there are notable opportunities for improvement in error handling, resource management, and scalability.

---

## Architecture Overview

### Core Components

1. **Task Scheduler (`majjen.c/h`)**: Cooperative non-preemptive scheduler
2. **Server Layer (`w_server.c/h`)**: Socket initialization and listen loop
3. **Client Layer (`w_client.c/h`)**: Per-connection state machine
4. **HTTP Parser (`HTTPParser.c/h`)**: Request/response parsing (unused)
5. **Utilities**: Linked list, sleep functions

### Data Flow

```
main() → creates scheduler & server
       → server adds listen task to scheduler
       → scheduler runs in infinite loop
           → listen task accepts connections
           → creates client task per connection
           → client task runs state machine (READ → PARSE → PROCESS → SEND → DONE)
```

---

## Detailed Component Analysis

### 1. Task Scheduler (`majjen.c/h`)

**Strengths:**
- Clean API with create/run/cleanup lifecycle
- Double-pointer pattern for safe task removal (`current_task`)
- Fixed array prevents allocation failures during runtime
- Proper resource cleanup with custom cleanup callbacks

**Critical Issues:**

#### 1.1 Fixed Task Limit (MAX_TASKS = 5)
```c
#define MAX_TASKS 5
```
**Severity:** CRITICAL  
**Impact:** Server can only handle 4 concurrent clients (1 slot for listening task)

**Recommendation:** 
- Increase to at least 128 for production
- Consider dynamic allocation with growth strategy
- Add metrics/warnings when approaching capacity

#### 1.2 Busy-Loop Design
```c
while (scheduler->task_count > 0) {
    for (int i = 0; i < MAX_TASKS; i++) {
        // ... run tasks ...
    }
}
```
**Severity:** HIGH  
**Impact:** 100% CPU usage even when idle

**Recommendation:**
- Implement `select()` or `epoll()` for I/O multiplexing
- Add yield/sleep mechanism for tasks with no work
- Consider event-driven architecture

#### 1.3 No Task Priority or Fairness
**Issue:** All tasks get equal CPU time regardless of workload

**Recommendation:**
- Add priority levels
- Implement work-stealing or time-slicing

#### 1.4 Error Handling
```c
if (scheduler->task_count >= MAX_TASKS) {
    errno = ENOMEM;
    return -1;
}
```
**Strength:** Consistent errno usage  
**Weakness:** Caller in `w_server.c` doesn't check `mj_scheduler_task_add()` return value

---

### 2. Server Layer (`w_server.c/h`)

**Strengths:**
- Uses `getaddrinfo()` for IPv4/IPv6 compatibility
- Non-blocking sockets with `O_NONBLOCK`
- `SO_REUSEADDR` for quick restarts
- Proper address validation

**Issues:**

#### 2.1 Memory Leak in Server Creation
```c
mj_task* task = calloc(1, sizeof(*task));
// ... 
if (server->last_error == W_SERVER_ERROR_NONE) {
    return server;
}
// Unknown error
free(task);
free(server);
return NULL;
```
**Severity:** LOW  
**Issue:** Error path at end is unreachable since `last_error` is `W_SERVER_ERROR_NONE` or already handled

#### 2.2 Unchecked Return Value
```c
mj_scheduler_task_add(scheduler, server->w_server_listen_task);
```
**Location:** `main.c:50`  
**Severity:** MEDIUM  
**Issue:** If scheduler is full, this fails silently but is checked in main

#### 2.3 Listen Task Placement
```c
void w_server_listen_TCP_nonblocking(mj_scheduler* scheduler, void* ctx)
```
**Code Comment Acknowledged:**
> "TODO: This should be a separate module that plugs into the server"

**Recommendation:** Create `src/listeners/tcp_nonblocking.c` module

#### 2.4 Incomplete Cleanup
```c
void w_server_listen_TCP_nonblocking_cleanup(...) {
    printf("Listening stopped on socket %d\n", server->listen_fd);
}
```
**Severity:** HIGH  
**Missing:**
- `close(server->listen_fd)`
- Active client termination
- Resource deallocation

#### 2.5 Missing Error Propagation
**Issue:** `w_server_error` enum defined but `last_error` field rarely set or checked

---

### 3. Client Layer (`w_client.c/h`)

**Strengths:**
- Clear state machine design
- Comprehensive error enum
- Proper context initialization
- Timestamp tracking for metrics

**Critical Issues:**

#### 3.1 Incomplete Implementation
**Current State:** Stub code with `sleep_ms()` delays

```c
case W_CLIENT_READING:
    printf("Client %d: CONNECTED\n", client->fd);
    sleep_ms(DEBUG_SLEEP_MS);  // ❌ No actual reading
    client->state = W_CLIENT_PARSING;
    break;
```

**Missing:**
- Actual `recv()` calls with EAGAIN handling
- Buffer accumulation logic
- Request header parsing
- Body length validation

#### 3.2 Hardcoded Response
```c
const char* msg = "DONE. CLOSING CONNECTION\n";
```
**Severity:** HIGH  
**Issue:** No HTTP response formatting, no content from HTTP parser

#### 3.3 Send Loop Issues
```c
ssize_t n = send(client->fd, client->response_data + client->response_sent,
                 client->response_len - client->response_sent, 0);
if (n > 0) {
    client->response_sent += n;
}
```
**Strength:** Handles partial sends correctly  
**Weakness:** No timeout mechanism, client could stall forever

#### 3.4 Resource Management
```c
void w_client_cleanup(mj_scheduler* scheduler, void* ctx) {
    w_client* client = (w_client*)ctx;
    if (client->fd >= 0) {
        close(client->fd);
        client->fd = -1;
    }
}
```
**Missing:**
- `free(client->response_data)`
- `free(client->request_body)`
- `free(client->request_body_raw)`
- Parsed request cleanup

#### 3.5 Buffer Overflow Risk
```c
#define W_CLIENT_READ_BUFFER_SIZE 8192
char read_buffer[W_CLIENT_READ_BUFFER_SIZE];
```
**Issue:** Fixed buffer with no overflow protection in future `recv()` implementation

**Recommendation:**
```c
size_t remaining = W_CLIENT_READ_BUFFER_SIZE - client->bytes_read;
if (remaining == 0) {
    client->error_code = W_CLIENT_ERROR_REQUEST_TOO_LARGE;
    client->state = W_CLIENT_DONE;
    return;
}
ssize_t n = recv(client->fd, client->read_buffer + client->bytes_read, remaining, 0);
```

---

### 4. HTTP Parser (`HTTPParser.c/h`)

**Strengths:**
- Comprehensive HTTP/1.1 support
- Proper string parsing with bounds checking
- Memory safety with `substr()` helper
- Linked list for headers (dynamic size)

**Issues:**

#### 4.1 Not Integrated
**Severity:** HIGH  
**Issue:** Compiled but unused by client handler

#### 4.2 Memory Management Complexity
```c
HTTPRequest* HTTPRequest_fromstring(const char* message) {
    // ... lots of allocations ...
    char* method = substr(current_line, space1);
    char* path = substr(space1 + 1, space2);
    char* protocol = substr(space2 + 1, current_line + length);
}
```
**Risk:** Multiple allocation points with early exits may leak memory

**Recommendation:** Audit all return/break paths for memory leaks

#### 4.3 Fixed URL Length Check
```c
if (space2 - (space1 + 1) >= MAX_URL_LEN) {
    printf("INVALID: Request URL is too long\n\n");
    request->reason = URLTooLong;
    // ...
}
```
**Good:** Validates before allocation  
**Issue:** `MAX_URL_LEN = 256` is quite small for modern URLs

#### 4.4 No Body Parsing
**Missing:** Request/response body handling beyond headers

---

### 5. Linked List (`linked_list.c/h`)

**Strengths:**
- Bidirectional traversal
- Proper head/tail management
- Safe disposal with double-pointers
- Efficient insert at arbitrary positions

**Issues:**

#### 5.1 Linear Search
```c
Node* LinkedList_get_index(LinkedList* list, size_t index) {
    // ... loops through nodes ...
}
```
**Performance:** O(n) for indexed access

**Note:** Acceptable for header lists (typically < 20 items)

#### 5.2 No Iterator Safety
**Risk:** Modifying list during `LinkedList_foreach` macro iteration

---

### 6. Build System (Makefile)

**Strengths:**
- Automatic dependency generation with `-MMD -MP`
- Recursive source discovery
- AddressSanitizer enabled for debugging

**Issues:**

#### 6.1 Debug-Only Configuration
```makefile
CFLAGS := -g -Wall -Wextra -std=c99 -fsanitize=address
```
**Missing:** Production build target without ASAN

**Note:** `Makefile_release` exists but not documented

#### 6.2 No Install Target
**Missing:** Standard `make install` for deployment

---

## Security Analysis

### Memory Safety ⚠️

**AddressSanitizer Enabled:** ✅ Good for development  
**Risks:**
- Incomplete cleanup paths may leak memory
- Fixed buffers in client without bounds checking
- No heap overflow protection in production build

### Network Security 🔴

**Critical Issues:**
1. **No request size limits enforced:** Client can send unlimited data
2. **No timeout handling:** Connections can hang indefinitely
3. **No rate limiting:** Vulnerable to resource exhaustion
4. **No input validation:** HTTP parser accepts malformed requests

### Denial of Service 🔴

**Attack Vectors:**
1. **Slot exhaustion:** Connect 5 clients, server stops accepting
2. **Slowloris:** Send partial requests slowly, hold connections
3. **CPU exhaustion:** Busy-loop scheduler enables CPU DoS

---

## Performance Analysis

### CPU Usage 🔴
- **100% CPU** even with zero clients (busy-loop)
- No yield mechanism for idle tasks

### Memory Usage ✅
- Fixed allocations prevent memory spikes
- Per-client overhead: ~9KB (`w_client` struct + buffers)

### Scalability 🔴
- **Hard limit:** 4 concurrent clients
- **Busy-loop:** Scales poorly with increased connections

### Latency 🟡
- Non-blocking I/O reduces blocking
- Busy-loop provides fast task switching
- `sleep_ms()` calls in debug code add artificial delays

---

## Code Quality

### Positive Aspects ✅
1. **Consistent naming conventions** (`w_*` for weather, `mj_*` for scheduler)
2. **Clear file organization** (libs/, separate concerns)
3. **Good comments** (especially TODOs marking incomplete work)
4. **Error enums** for each module
5. **Double-pointer cleanup** prevents dangling references

### Areas for Improvement 🟡

#### 1. Error Handling Inconsistency
```c
// main.c - checks return
if (!scheduler) {
    fprintf(stderr, "Failed to create scheduler\n");
    return 1;
}

// w_server.c - sometimes uses last_error, sometimes not
```

#### 2. Magic Numbers
```c
#define DEBUG_SLEEP_MS 10
char address[46];  // Why 46? (Answer: INET6_ADDRSTRLEN, but not documented)
```

#### 3. Printf Debugging
```c
printf("[Client %d] READING\n", client->fd);  // Commented out
printf("Client %d: CONNECTED\n", client->fd);  // Left in
```
**Recommendation:** Implement proper logging with levels (DEBUG/INFO/ERROR)

#### 4. Unused Fields
```c
typedef struct w_server {
    size_t active_count;  // Never incremented
    w_server_error last_error;  // Rarely set
```

#### 5. No Unit Tests
**Missing:** Test infrastructure for parser, linked list, scheduler

---

## Critical TODOs from Codebase

### From `w_server.c`:
```c
// TODO w_server_listen_TCP_nonblocking() gets added as a task
// Therefore it should be a separate module that plugs into the server.

// TODO finish clean up function

// TODO server init function, error handling:
// - Should we only return a server on no errors?
```

### From `w_client.c`:
```c
// TODO: Read message
// TODO: Parse message  
// TODO: Handle the request, e.g., GET /weather?city=Stockholm
```

---

## Recommendations by Priority

### 🔴 Critical (Must Fix Before Production)

1. **Increase MAX_TASKS** to at least 128
2. **Implement actual HTTP request reading** in `W_CLIENT_READING`
3. **Integrate HTTP parser** into client state machine
4. **Fix memory leaks** in client cleanup (response_data, request_body)
5. **Add connection timeouts** (idle timeout, read timeout, write timeout)
6. **Replace busy-loop** with `select()`/`epoll()`

### 🟡 High Priority (Should Address Soon)

7. **Add request size limits** and enforce them
8. **Implement proper logging** system (replace printf)
9. **Complete cleanup functions** (server, client)
10. **Add rate limiting** per IP address
11. **Audit HTTP parser** for memory leaks
12. **Add unit tests** for critical components

### 🟢 Medium Priority (Improvements)

13. **Create listener abstraction** (TCP, UDP, TLS)
14. **Add metrics collection** (requests/sec, avg latency)
15. **Implement graceful shutdown** (SIGINT/SIGTERM handler)
16. **Add configuration file** support (JSON/TOML)
17. **Document error codes** and recovery strategies
18. **Create production Makefile** target

### 🔵 Low Priority (Nice to Have)

19. **Add keep-alive support** (HTTP/1.1 persistent connections)
20. **Implement chunked transfer encoding**
21. **Add gzip compression** support
22. **Create admin/health endpoint** (`/health`, `/metrics`)
23. **Add connection pooling** for backend services
24. **Implement middleware system** (use empty `middleware/` directory)

---

## Specific Code Improvements

### Example 1: Fix Client Cleanup
```c
// BEFORE:
void w_client_cleanup(mj_scheduler* scheduler, void* ctx) {
    w_client* client = (w_client*)ctx;
    if (client->fd >= 0) {
        close(client->fd);
        client->fd = -1;
    }
}

// AFTER:
void w_client_cleanup(mj_scheduler* scheduler, void* ctx) {
    w_client* client = (w_client*)ctx;
    
    // Close socket
    if (client->fd >= 0) {
        close(client->fd);
        client->fd = -1;
    }
    
    // Free allocated buffers
    free(client->response_data);
    client->response_data = NULL;
    
    free(client->request_body);
    client->request_body = NULL;
    
    free(client->request_body_raw);
    client->request_body_raw = NULL;
    
    // Clean up parsed request if exists
    if (client->parsed_request) {
        HTTPRequest_Dispose((HTTPRequest**)&client->parsed_request);
    }
}
```

### Example 2: Replace Busy-Loop with select()
```c
// In majjen.c - mj_scheduler_run()
int mj_scheduler_run(mj_scheduler* scheduler) {
    if (scheduler == NULL) {
        errno = EINVAL;
        return -1;
    }
    
    while (scheduler->task_count > 0) {
        fd_set readfds, writefds;
        FD_ZERO(&readfds);
        FD_ZERO(&writefds);
        int maxfd = 0;
        
        // Let each task register its FDs and desired events
        for (int i = 0; i < MAX_TASKS; i++) {
            mj_task* task = scheduler->task_list[i];
            if (task && task->register_fds) {
                task->register_fds(task->ctx, &readfds, &writefds, &maxfd);
            }
        }
        
        // Wait for I/O events (with timeout)
        struct timeval tv = {.tv_sec = 1, .tv_usec = 0};
        int nready = select(maxfd + 1, &readfds, &writefds, NULL, &tv);
        
        if (nready < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        
        // Run tasks that have work to do
        for (int i = 0; i < MAX_TASKS; i++) {
            mj_task* task = scheduler->task_list[i];
            if (task && task->run) {
                scheduler->current_task = &scheduler->task_list[i];
                task->run(scheduler, task->ctx);
                scheduler->current_task = NULL;
            }
        }
    }
    return 0;
}
```

### Example 3: Add Connection Timeout
```c
// In w_client.h, add to w_client struct:
struct timespec last_activity;
int timeout_seconds;

// In w_client_run(), add timeout check:
void w_client_run(mj_scheduler* scheduler, void* ctx) {
    w_client* client = (w_client*)ctx;
    
    // Check for timeout
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    time_t elapsed = now.tv_sec - client->last_activity.tv_sec;
    
    if (elapsed > client->timeout_seconds) {
        fprintf(stderr, "Client %d timeout after %ld seconds\n", 
                client->fd, elapsed);
        client->error_code = W_CLIENT_ERROR_TIMEOUT;
        client->state = W_CLIENT_DONE;
        return;
    }
    
    // ... rest of state machine ...
    
    // Update last activity on successful I/O
    clock_gettime(CLOCK_MONOTONIC, &client->last_activity);
}
```

---

## Architecture Decision Records

### ADR-1: Cooperative vs Preemptive Scheduling
**Decision:** Cooperative task scheduler  
**Rationale:** Simpler to implement, no thread synchronization needed  
**Consequences:** 
- ✅ Reduced complexity
- ✅ No race conditions
- ❌ One blocking task stalls entire server
- ❌ No true parallelism

**Recommendation:** Acceptable for I/O-bound workload with proper select() usage

### ADR-2: Fixed Task Array vs Dynamic Growth
**Decision:** Fixed array of 5 tasks  
**Rationale:** Predictable memory usage, no runtime allocation failures  
**Consequences:**
- ✅ No malloc() failures during request handling
- ❌ Hard client limit
- ❌ Not production-ready

**Recommendation:** Increase to 128-256 for MVP, consider hybrid approach

### ADR-3: State Machine vs Thread-Per-Client
**Decision:** State machine per client  
**Rationale:** Aligns with cooperative scheduler model  
**Consequences:**
- ✅ Low memory overhead
- ✅ No context switching overhead
- ✅ Explicit state management
- ❌ Callback-style code complexity

**Recommendation:** Good choice for this architecture

---

## Conclusion

The UB-WeatherServer demonstrates a **solid foundation** with clean separation of concerns and thoughtful design patterns. The cooperative scheduler and state machine approach are well-suited for a lightweight HTTP server.

**However**, the codebase is **not production-ready**:
- Critical functionality is incomplete (HTTP handling)
- Hard limit of 4 concurrent clients
- 100% CPU usage from busy-loop
- Missing error handling and timeout mechanisms
- Potential memory leaks

**Estimated Completion:** With focused development, this could be production-ready in:
- **MVP (basic functionality):** 2-3 weeks
- **Production-hardened:** 1-2 months with testing

**Key Success Factors:**
1. Complete the HTTP request/response cycle
2. Implement select()/epoll() for I/O multiplexing
3. Add comprehensive error handling and timeouts
4. Increase task capacity
5. Add logging and metrics
6. Write integration tests

The architecture is sound – it's an implementation completion problem, not a design problem.

---

**End of Review**

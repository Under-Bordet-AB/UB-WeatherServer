(Prompt claude 4.5: Look through this project and note the design of the implementation. I want you to tell me about all the design patterns that appear and are used in the code. Also comment on alternative solutions and how "canonical" or correct the current use of patterns are. End with a actionable todo list for the whole project divided into topics for easy distribution of tasks on a team.)

# Architecture Analysis & Design Patterns — UB-WeatherServer

**Date:** November 8, 2025  
**Branch:** main  
**Purpose:** Comprehensive design pattern analysis with actionable improvement roadmap

---

## Executive Summary

UB-WeatherServer implements a single-threaded, event-driven HTTP weather API server using cooperative multitasking. The architecture follows solid layering principles (transport → protocol → application) but has several implementation gaps that impact robustness, performance, and maintainability.

**Key strengths:**
- Clean layered architecture with separation of concerns
- Consistent naming and object lifecycle patterns
- Non-blocking I/O throughout the stack
- State machine approach for connection handling

**Critical gaps:**
- Scheduler lacks yielding/blocking mechanisms (burns CPU)
- No graceful shutdown or resource cleanup on error paths
- Memory leaks in connection disposal
- Missing backpressure and resource limits
- Incomplete error handling and logging

---

## Design Patterns Analysis

### 1. Cooperative Multitasking Pattern ⚠️

**Implementation:**
- Custom scheduler (`smw` = "Simple Multitasking Worker")
- Global singleton with fixed 16-task array
- Tasks poll via callback: `void (*callback)(void* context, uint64_t monTime)`
- Main loop calls `smw_work()` every 10ms

**Architecture:**
```
main() 
  └─> while(1) { smw_work(now); usleep(10000); }
       └─> for each task: task->callback(ctx, now)
            └─> TCPServer_TaskWork → accept clients
            └─> HTTPServerConnection_TaskWork → drive connection FSM
            └─> WeatherServer_TaskWork → iterate instances
```

**Issues:**
- ❌ **CPU waste:** 10ms polling burns ~10% CPU even when idle
- ❌ **No yielding:** Tasks can't suspend and wake on events
- ❌ **No prioritization:** All tasks run sequentially every tick
- ❌ **Fixed limits:** Hard cap at 16 tasks (DoS vulnerability)
- ❌ **Global state:** Singleton prevents multiple servers or testing

**Canonical alternative:** epoll-based event loop (libuv, libev) or at minimum integrate `epoll_wait` with computed timeout.

**Impact:** HIGH — causes performance problems and limits scalability

---

### 2. Layered Architecture Pattern ✅

**Implementation:**
```
Application:     WeatherServer, WeatherServerInstance
Protocol:        HTTPServer, HTTPServerConnection, HTTPParser
Transport:       TCPServer, TCPClient
Infrastructure:  smw (scheduler), LinkedList, utils
```

**Assessment:**
- ✅ Clear responsibility boundaries
- ✅ Lower layers have no knowledge of upper layers
- ✅ Protocol layer is reusable (HTTP could serve any app)
- ⚠️ Some abstraction leaks (e.g., HTTPServerConnection state enum exposed publicly)

**Canonical pattern:** This follows the **OSI Model** / **Protocol Stack** pattern correctly.

**Impact:** LOW — architecture is sound; minor encapsulation improvements needed

---

### 3. State Machine Pattern ⚠️

**Implementation:**

**HTTPServerConnection states:**
```c
Init → Reading → Parsing → Wait → Send → Done/Dispose/Failed/Timeout
```

**WeatherServerInstance states:**
```c
Waiting → Init → Work → Done → Dispose
```

**Driving logic:**
- `HTTPServerConnection_TaskWork()` — explicit switch statement with state transitions
- `WeatherServerInstance_Work()` — explicit switch statement

**Issues:**
- ⚠️ **Incomplete transitions:** Some states (e.g., `Wait`) are entered but never exited properly
- ⚠️ **Missing timeout integration:** Timeout checked globally, not per-state
- ⚠️ **No return codes:** State functions don't signal "done" or "reschedule" to scheduler
- ⚠️ **Redundant state assignment:** `WeatherServerInstance_Work()` ends with unconditional `state = Done` (overrides switch logic)

**Example bug in WeatherServerInstance.c:**
```c
switch (_Server->state) {
  case Work: {
    // ... do work, set state = Done
    break;
  }
  // ...
}
_Server->state = WeatherServerInstance_State_Done; // ← OVERRIDES switch!
```

**Canonical pattern:** Explicit FSM with `step()` returning next action code is the standard approach.

**Impact:** MEDIUM — causes logic bugs and makes debugging harder

---

### 4. Callback/Observer Pattern ✅

**Implementation:**
- Used throughout for async notifications:
  - `TCPServer_OnAccept` — new client FD available
  - `HTTPServer_OnConnection` — new HTTP connection created
  - `HTTPServerConnection_OnRequest` — complete request parsed
- Context pointers (`void*`) passed through callback chains

**Example chain:**
```
TCPServer (accept FD)
  → HTTPServer_OnAccept (wrap in HTTPServerConnection)
    → HTTPServer.onConnection (notify app)
      → WeatherServer_OnHTTPConnection (create instance)
        → HTTPServerConnection.onRequest (notify session)
          → WeatherServerInstance_OnRequest (handle route)
```

**Issues:**
- ⚠️ **Type-unsafe:** Heavy use of `void*` casting loses compile-time safety
- ⚠️ **Inconsistent return codes:** Some callbacks return `int` (ignored), others `void`
- ⚠️ **No error propagation:** Errors in callbacks don't bubble up

**Canonical alternative:** Use typed context structs or vtable pattern for polymorphism.

**Impact:** MEDIUM — causes type-safety issues but is functional

---

### 5. Object-Oriented C (Manual) ✅

**Implementation:**
- Structs as objects: `WeatherServer`, `HTTPServer`, etc.
- Constructor/destructor pairs:
  - `Type_Initiate(Type* obj)` — initialize existing object
  - `Type_InitiatePtr(Type** ptr)` — allocate + initialize
  - `Type_Dispose(Type* obj)` — cleanup
  - `Type_DisposePtr(Type** ptr)` — cleanup + free + null pointer
- Composition: structs embed other structs (e.g., `WeatherServer` contains `HTTPServer`)

**Example:**
```c
typedef struct {
    HTTPServer httpServer;  // composition
    LinkedList* instances;  // aggregation
    smw_task* task;         // reference
} WeatherServer;
```

**Issues:**
- ⚠️ **Manual memory management burden:** Easy to leak if Dispose not called
- ⚠️ **Incomplete error cleanup:** Some `Initiate` functions don't clean up on partial failure
- ⚠️ **No ownership documentation:** Unclear who owns/frees pointers

**Canonical pattern:** Standard C idiom used in GLib, FFmpeg, etc.

**Impact:** LOW — pattern is correct; needs better error handling

---

### 6. Factory Pattern (Initiate functions) ✅

**Implementation:**
- Two-phase construction for flexibility:
  ```c
  // Stack allocation
  WeatherServer srv;
  WeatherServer_Initiate(&srv);
  
  // Heap allocation
  WeatherServer* srv;
  WeatherServer_InitiatePtr(&srv);
  ```

**Issues:**
- ⚠️ **Inconsistent malloc checking:** Some paths don't check allocation failure
- ⚠️ **Partial init cleanup missing:** If `Initiate` fails mid-way, resources may leak

**Impact:** LOW — pattern is sound; needs defensive coding

---

### 7. Resource Acquisition Is Initialization (RAII-like) ⚠️

**Implementation:**
- Resources acquired in `Initiate`, released in `Dispose`
- `DisposePtr` nulls pointers to prevent use-after-free

**Issues:**
- ❌ **Memory leaks:**
  - `WeatherServerInstance` never removed from `WeatherServer.instances` list
  - Connection objects not freed on timeout/error in many paths
  - `writeBuffer` in `HTTPServerConnection` leaked (allocated by `HTTPResponse_tostring`, never freed properly)
- ❌ **Double-free risks:** No refcounting; unclear ownership of shared objects
- ❌ **No cleanup-on-error:** Partial initialization leaves resources dangling

**Example leak in HTTPServerConnection.c:**
```c
void HTTPServerConnection_SendResponse(...) {
    HTTPResponse *resp = HTTPResponse_new(_responseCode, _responseBody);
    char *message = (char *)HTTPResponse_tostring(resp);
    _Connection->writeBuffer = (uint8_t *)message;  // ← who frees this?
    HTTPResponse_Dispose(&resp);
    // message is orphaned!
}
```

**Impact:** HIGH — causes memory leaks under load

---

### 8. Iterator Pattern (LinkedList_foreach macro) ✅

**Implementation:**
```c
#define LinkedList_foreach(list, node) \
    for (Node* node = (list)->head; node != NULL; node = node->front)

// Usage:
LinkedList_foreach(_Server->instances, node) {
    WeatherServerInstance* instance = (WeatherServerInstance*)node->item;
    WeatherServerInstance_Work(instance, _MonTime);
}
```

**Issues:**
- ⚠️ **Unsafe for removal:** Removing items during iteration breaks the loop
- ⚠️ **Exposes internals:** Caller must know about `node->item` casting

**Impact:** LOW — convenient but needs "safe removal" variant

---

### 9. Singleton Pattern (Global Scheduler) ❌

**Implementation:**
```c
smw g_smw;  // global instance
```

**Issues:**
- ❌ **Untestable:** Can't have multiple schedulers for unit tests
- ❌ **Not reusable:** Can't run multiple servers in one process
- ❌ **Thread-unsafe:** Global mutable state with no locking

**Impact:** MEDIUM — limits flexibility and testing

---

### 10. Dependency Injection (via Callbacks) ✅

**Implementation:**
- Callbacks injected at construction:
  ```c
  TCPServer_Initiate(&srv->tcpServer, "8080", HTTPServer_OnAccept, srv);
  ```
- Decouples layers and enables testing

**Impact:** LOW — well implemented

---

## Critical Bugs & Anti-Patterns

### Bug 1: Memory Leak in Connection Lifecycle ❌

**Location:** `WeatherServer.c`, `HTTPServerConnection.c`

**Problem:**
1. `WeatherServer_OnHTTPConnection` appends instances to list
2. Instances never removed from list
3. When connection times out/closes, instance remains in list forever
4. `LinkedList_foreach` iterates over dead connections

**Fix:**
```c
// In HTTPServerConnection_Dispose:
void HTTPServerConnection_Dispose(HTTPServerConnection *_Connection) {
    // ... existing cleanup ...
    
    // Notify parent to remove from list
    if (_Connection->onDispose) {
        _Connection->onDispose(_Connection->context);
    }
}

// In WeatherServer: add disposal callback to remove from list
```

---

### Bug 2: writeBuffer Memory Leak ❌

**Location:** `HTTPServerConnection.c:HTTPServerConnection_SendResponse`

**Problem:**
```c
char *message = (char *)HTTPResponse_tostring(resp);
_Connection->writeBuffer = (uint8_t *)message;
// message is never freed!
```

**Fix:**
```c
void HTTPServerConnection_Dispose(HTTPServerConnection *_Connection) {
    TCPClient_Dispose(&_Connection->tcpClient);
    if (_Connection->writeBuffer) {
        free(_Connection->writeBuffer);  // ← already present, but check if HTTPResponse_tostring returns malloc'd memory
        _Connection->writeBuffer = NULL;
    }
    if (_Connection->url) free(_Connection->url);      // ← MISSING
    if (_Connection->method) free(_Connection->method); // ← MISSING
    smw_destroyTask(_Connection->task);
}
```

---

### Bug 3: State Machine Override ❌

**Location:** `WeatherServerInstance.c:WeatherServerInstance_Work`

**Problem:**
```c
switch (_Server->state) {
    case Work: {
        // ... sets state = Done
        break;
    }
}
_Server->state = WeatherServerInstance_State_Done; // ← unconditional override!
```

**Fix:** Remove the unconditional assignment at the end.

---

### Bug 4: No Scheduler Blocking ❌

**Location:** `main.c`

**Problem:**
```c
while (1) {
    smw_work(now);
    usleep(10000);  // burns 10% CPU even when idle
}
```

**Fix:** Integrate epoll with timeout:
```c
while (1) {
    uint64_t next_deadline = smw_get_earliest_wakeup();
    int timeout_ms = compute_timeout(next_deadline);
    int n = epoll_wait(epfd, events, MAX, timeout_ms);
    // handle events, then run tasks
    smw_work(SystemMonotonicMS());
}
```

---

### Anti-Pattern 1: No Resource Limits ❌

**Issues:**
- No limit on concurrent connections (DoS vulnerability)
- No limit on request size (memory exhaustion)
- No limit on timeout queue depth

**Fix:** Add limits to HTTPServer config:
```c
typedef struct {
    int max_connections;
    size_t max_request_size;
    int timeout_ms;
} HTTPServerConfig;
```

---

### Anti-Pattern 2: No Structured Logging ❌

**Issues:**
- Scattered `printf` statements
- No log levels (debug, info, warn, error)
- No context (connection ID, timestamp, etc.)

**Fix:** Add minimal logging abstraction:
```c
typedef enum { LOG_DEBUG, LOG_INFO, LOG_WARN, LOG_ERROR } LogLevel;
void log_msg(LogLevel level, const char* fmt, ...);
```

---

## Missing Patterns / Features

1. **No graceful shutdown** — server can't drain connections and stop cleanly
2. **No backpressure** — if app is slow, incoming requests queue unbounded
3. **No health/metrics endpoint** — can't monitor server health (partially present: `/health` route)
4. **No configuration management** — hardcoded port, timeouts, buffer sizes
5. **No request routing framework** — manual `strcmp` in switch (should be table-driven)
6. **No middleware/filter chain** — can't add logging, auth, rate-limiting layers
7. **No connection pooling** — each request allocates/frees; should reuse
8. **No zero-copy buffers** — copies data multiple times (read → parse → app)

---

## Actionable Task List

### Topic 1: Core Scheduler Improvements (HIGH PRIORITY)

**Owner:** Infrastructure team  
**Effort:** 5-8 days  
**Dependencies:** None

- [ ] **Task 1.1:** Replace global `g_smw` with allocated `smw_t*` instance
  - Change API: `smw_t* smw_create(); void smw_destroy(smw_t*);`
  - Pass scheduler handle to all functions
  - Update all call sites

- [ ] **Task 1.2:** Add task state and return codes
  - Add `task_state_t` enum: `READY, WAITING, DONE`
  - Change callback signature: `int (*callback)(void*, uint64_t)` returning reschedule delay
  - Track per-task `wakeup_ms` deadline

- [ ] **Task 1.3:** Integrate epoll for I/O events
  - Create `epoll_fd` in scheduler
  - Add `smw_register_fd(task, fd, events)` API
  - Compute timeout from earliest `wakeup_ms`
  - Call `epoll_wait()` instead of busy sleep

- [ ] **Task 1.4:** Add dynamic task storage
  - Replace fixed 16-task array with linked list or priority queue
  - Add task allocation/deallocation
  - Add max_tasks limit check with error handling

- [ ] **Task 1.5:** Add graceful shutdown
  - Add `smw_shutdown(smw_t*)` to stop event loop
  - Drain pending tasks with timeout
  - Close all FDs and free resources

**Acceptance:** Scheduler uses <1% CPU when idle, handles 1000+ concurrent connections

---

### Topic 2: Memory Management & Resource Cleanup (HIGH PRIORITY)

**Owner:** Platform team  
**Effort:** 3-5 days  
**Dependencies:** None

- [ ] **Task 2.1:** Fix connection lifecycle memory leaks
  - Add `HTTPServerConnection_onDispose` callback
  - Remove disposed instances from `WeatherServer.instances` list
  - Add reference counting or ownership docs

- [ ] **Task 2.2:** Fix buffer memory leaks
  - Free `writeBuffer` in `HTTPServerConnection_Dispose` (verify `HTTPResponse_tostring` ownership)
  - Free `url` and `method` strings in dispose
  - Add valgrind/ASAN test to verify no leaks

- [ ] **Task 2.3:** Add cleanup-on-error in all Initiate functions
  - Audit all `*_Initiate` functions
  - Add goto-based cleanup labels or explicit cleanup on failure
  - Write unit tests for partial-init scenarios

- [ ] **Task 2.4:** Document ownership conventions
  - Add comment headers to all types: "Ownership: caller/callee"
  - Document which functions take/transfer ownership
  - Add `_borrow` / `_transfer` naming convention

**Acceptance:** Zero memory leaks under valgrind for 10k request test

---

### Topic 3: State Machine Robustness (MEDIUM PRIORITY)

**Owner:** Protocol team  
**Effort:** 2-3 days  
**Dependencies:** Task 1.2 (return codes)

- [ ] **Task 3.1:** Fix state machine bugs
  - Remove unconditional `state = Done` in `WeatherServerInstance_Work`
  - Add explicit state transitions for all paths
  - Add state validation (detect invalid transitions)

- [ ] **Task 3.2:** Add per-state timeout tracking
  - Track `entered_state_at` timestamp
  - Check timeout per state (not globally)
  - Add per-state timeout config

- [ ] **Task 3.3:** Add state machine unit tests
  - Test all valid transitions
  - Test timeout handling per state
  - Test error paths (EAGAIN, partial reads, etc.)

- [ ] **Task 3.4:** Return reschedule delays from state functions
  - Change signature: `int session_step(session_t*, uint64_t) -> delay_ms`
  - Integrate with scheduler wakeup mechanism
  - Document return code semantics

**Acceptance:** All state transitions tested; no stuck connections

---

### Topic 4: Error Handling & Logging (MEDIUM PRIORITY)

**Owner:** Platform team  
**Effort:** 2-4 days  
**Dependencies:** None

- [ ] **Task 4.1:** Add structured logging framework
  - Define log levels: DEBUG, INFO, WARN, ERROR
  - Add `log_msg(level, fmt, ...)` function
  - Add per-connection context (conn_id, peer_addr)

- [ ] **Task 4.2:** Replace all printf with log_msg
  - Audit all `printf` calls
  - Add appropriate log levels
  - Add connection/request context

- [ ] **Task 4.3:** Standardize error codes
  - Define error enums: `ERR_OK=0, ERR_NOMEM=-1, ERR_INVALID=-2, ...`
  - Return error codes consistently from all functions
  - Add error-to-string helper

- [ ] **Task 4.4:** Add error context propagation
  - Add `error_context_t` struct with code + message
  - Pass error context through callback chains
  - Log errors with full context

**Acceptance:** All errors logged with context; no raw printf calls

---

### Topic 5: HTTP Protocol Improvements (MEDIUM PRIORITY)

**Owner:** Protocol team  
**Effort:** 3-5 days  
**Dependencies:** Task 3.2 (state machine)

- [ ] **Task 5.1:** Add incremental HTTP parser
  - Implement parser state machine (see `V2/parsing_guide.md`)
  - Handle partial reads correctly
  - Support Content-Length and chunked encoding

- [ ] **Task 5.2:** Add early method validation
  - Check first byte as soon as data arrives
  - Reject unsupported methods immediately
  - Add configurable allowed-methods list

- [ ] **Task 5.3:** Add keep-alive support
  - Parse `Connection:` header
  - Reuse connections for multiple requests
  - Add idle timeout for keep-alive connections

- [ ] **Task 5.4:** Add HTTP/1.1 compliance
  - Handle `Host:` header requirement
  - Add `Date:` and `Server:` response headers
  - Support `Expect: 100-continue`

**Acceptance:** Passes HTTP/1.1 compliance test suite

---

### Topic 6: Application Layer (LOW PRIORITY)

**Owner:** App team  
**Effort:** 2-3 days  
**Dependencies:** Task 5.3 (keep-alive)

- [ ] **Task 6.1:** Implement routing table
  - Replace strcmp switch with table: `{ method, path_regex, handler }`
  - Add route registration API
  - Add path parameter extraction

- [ ] **Task 6.2:** Add middleware chain
  - Define middleware signature: `int (*mw)(request, response, next)`
  - Add logging, auth, rate-limiting middleware
  - Chain middleware before route handlers

- [ ] **Task 6.3:** Implement weather API routes
  - `/cities` — return JSON list
  - `/weather/{city}` — fetch weather (mock or real API)
  - Add error responses (404, 400, 500)

- [ ] **Task 6.4:** Add JSON serialization helpers
  - Create `json_builder` utility
  - Add JSON escaping and validation
  - Add Content-Type header management

**Acceptance:** All routes functional with proper JSON responses

---

### Topic 7: Configuration & Limits (LOW PRIORITY)

**Owner:** Infrastructure team  
**Effort:** 1-2 days  
**Dependencies:** Task 1.4 (dynamic tasks)

- [ ] **Task 7.1:** Add configuration struct
  - Define `server_config_t` with all tunables
  - Load from file or command-line args
  - Add validation and defaults

- [ ] **Task 7.2:** Add resource limits
  - Max concurrent connections
  - Max request size (headers + body)
  - Max URL length, max header count
  - Connection timeout, idle timeout

- [ ] **Task 7.3:** Add backpressure
  - Stop accepting when at connection limit
  - Add "503 Service Unavailable" response
  - Add metrics for rejected connections

**Acceptance:** Server handles resource exhaustion gracefully

---

### Topic 8: Testing & Validation (ONGOING)

**Owner:** QA team  
**Effort:** 2 days per milestone  
**Dependencies:** Each topic above

- [ ] **Task 8.1:** Add unit tests
  - Test scheduler task lifecycle
  - Test state machine transitions
  - Test HTTP parser edge cases
  - Test memory cleanup

- [ ] **Task 8.2:** Add integration tests
  - Test full request/response cycle
  - Test concurrent connections
  - Test timeout handling
  - Test error paths

- [ ] **Task 8.3:** Add performance benchmarks
  - Measure requests/second
  - Measure latency (p50, p99)
  - Measure memory usage under load
  - Measure CPU usage when idle

- [ ] **Task 8.4:** Add fuzz testing
  - Fuzz HTTP parser with malformed requests
  - Fuzz socket I/O with partial reads/writes
  - Run with ASAN/UBSAN

**Acceptance:** 90%+ code coverage; no crashes under fuzz

---

### Topic 9: V2 Migration (STRETCH GOAL)

**Owner:** Architecture team  
**Effort:** 10-15 days  
**Dependencies:** All above tasks

- [ ] **Task 9.1:** Port improvements to V2 scaffold
  - Implement scheduler with epoll
  - Implement state machines with return codes
  - Add all missing features from above

- [ ] **Task 9.2:** Migrate incrementally
  - Run V2 in parallel for testing
  - Gradually cut over traffic
  - Deprecate old codebase

**Acceptance:** V2 handles production traffic

---

## Priority Matrix

| Priority | Topic | Effort | Impact | Risk |
|----------|-------|--------|--------|------|
| P0 | Scheduler (1) | High | Critical | Medium |
| P0 | Memory (2) | Medium | Critical | Low |
| P1 | State Machine (3) | Low | High | Low |
| P1 | Error Handling (4) | Medium | High | Low |
| P1 | HTTP Protocol (5) | Medium | High | Medium |
| P2 | Application (6) | Low | Medium | Low |
| P2 | Config/Limits (7) | Low | Medium | Low |
| P∞ | Testing (8) | Medium | High | Low |
| P3 | V2 Migration (9) | High | High | High |

---

## Comparison: Current vs V2 Scaffold

| Aspect | Current Main | V2 Scaffold | Winner |
|--------|--------------|-------------|--------|
| Scheduler | Global singleton, no yielding | Allocated, explicit yield | **V2** |
| State machines | Present but buggy | Clean switch + return codes | **V2** |
| Memory mgmt | Leaks present | Typed contexts, documented | **V2** |
| Naming | Mixed CamelCase | Consistent snake_case | **V2** |
| Layering | Good but leaky | Clean separation | **V2** |
| Docs | Minimal | Extensive guides | **V2** |
| Error handling | Inconsistent | Standardized codes | **V2** |
| Testing | None | Scaffold for tests | **V2** |

**Recommendation:** Fix P0 issues in main, then migrate to V2 architecture.

---

## Conclusion

The current codebase demonstrates solid architectural principles with clean layering and appropriate use of event-driven patterns. However, critical implementation gaps in the scheduler, memory management, and error handling limit production readiness.

**Immediate actions:**
1. Fix memory leaks (Topic 2) — 3-5 days
2. Add epoll to scheduler (Topic 1.3) — 2-3 days
3. Fix state machine bugs (Topic 3.1) — 1 day

**Medium-term:**
- Complete scheduler refactor (Topic 1) — 1-2 weeks
- Add error handling (Topic 4) — 1 week
- Improve HTTP compliance (Topic 5) — 1 week

**Long-term:**
- Migrate to V2 architecture (Topic 9) — 2-3 weeks

With these improvements, the server will be production-ready for moderate load (<10k concurrent connections). For higher scale, consider adopting a proven event loop library (libuv, libev) or migrating fully to V2.

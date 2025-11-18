# Design Patterns and Architectural Analysis

**Project:** UB-WeatherServer  
**Analysis Date:** November 18, 2025

---

## Table of Contents
1. [Design Patterns Identified](#design-patterns-identified)
2. [Architectural Patterns](#architectural-patterns)
3. [Anti-Patterns Found](#anti-patterns-found)
4. [Memory Management Strategy](#memory-management-strategy)
5. [Concurrency Model](#concurrency-model)
6. [Extension Points](#extension-points)

---

## Design Patterns Identified

### 1. State Pattern ✅

**Location:** `w_client.c`

```c
typedef enum {
    W_CLIENT_READING,
    W_CLIENT_PARSING,
    W_CLIENT_PROCESSING,
    W_CLIENT_SENDING,
    W_CLIENT_DONE
} w_client_state;

void w_client_run(mj_scheduler* scheduler, void* ctx) {
    w_client* client = (w_client*)ctx;
    switch (client->state) {
        case W_CLIENT_READING:   // ...
        case W_CLIENT_PARSING:   // ...
        case W_CLIENT_PROCESSING: // ...
        case W_CLIENT_SENDING:    // ...
        case W_CLIENT_DONE:       // ...
    }
}
```

**Benefits:**
- Clear state transitions
- Easy to debug (state is explicit)
- Prevents invalid state transitions
- Self-documenting code flow

**Implementation Quality:** 8/10
- Well-structured
- Missing: State transition validation
- Missing: State history for debugging

**Recommendation:**
```c
// Add state transition logging
void w_client_set_state(w_client* client, w_client_state new_state) {
    log_debug("Client %d: %s -> %s", client->fd, 
              state_to_string(client->state), 
              state_to_string(new_state));
    client->state = new_state;
}
```

---

### 2. Strategy Pattern (Implicit) ⚠️

**Location:** `mj_task` function pointers

```c
typedef struct mj_task {
    mj_task_fn create;
    mj_task_fn run;
    mj_task_fn cleanup;
    void* ctx;
} mj_task;
```

**Purpose:** Different task types (server listener, client handler) use different implementations

**Benefits:**
- Polymorphic behavior in C
- Runtime behavior customization
- Decoupled scheduler from task logic

**Issues:**
- `create` function pointer never used (always NULL)
- No task type identification mechanism
- No introspection capability

**Recommendation:**
```c
typedef enum {
    TASK_TYPE_LISTENER,
    TASK_TYPE_CLIENT,
    TASK_TYPE_WORKER,
    TASK_TYPE_TIMER
} mj_task_type;

typedef struct mj_task {
    mj_task_type type;
    const char* name;  // For debugging
    mj_task_fn create;
    mj_task_fn run;
    mj_task_fn cleanup;
    void* ctx;
} mj_task;
```

---

### 3. Object Pool Pattern (Fixed) 🔴

**Location:** `mj_scheduler` task array

```c
typedef struct mj_scheduler {
    mj_task* task_list[MAX_TASKS];
    size_t task_count;
} mj_scheduler;
```

**Intent:** Pre-allocated task slots for predictable memory usage

**Issues:**
- Size too small (MAX_TASKS = 5)
- No growing mechanism
- Wastes memory (always allocates max)
- Linear search for free slot

**Better Approach:**
```c
typedef struct mj_task_pool {
    mj_task** tasks;
    size_t capacity;
    size_t count;
    size_t high_water_mark;
} mj_task_pool;

// Start with small size, grow as needed
mj_task_pool* pool_create(size_t initial_capacity);
int pool_grow(mj_task_pool* pool);
```

---

### 4. Factory Pattern (Partial) ⚠️

**Location:** `w_client_create()`, `w_server_create()`

```c
mj_task* w_client_create(int client_fd) {
    mj_task* new_task = calloc(1, sizeof(*new_task));
    w_client* new_ctx = calloc(1, sizeof(*new_ctx));
    // ... initialization ...
    new_task->ctx = new_ctx;
    return new_task;
}
```

**Benefits:**
- Encapsulates complex initialization
- Single point of object creation
- Ensures proper setup

**Issues:**
- No error rollback (partial initialization on OOM)
- Caller must know to add task to scheduler
- No pooling/reuse mechanism

---

### 5. Dispose Pattern (Double-Pointer) ✅

**Location:** `HTTPRequest_Dispose()`, `LinkedList_dispose()`

```c
void HTTPRequest_Dispose(HTTPRequest** req) {
    if (req && *req) {
        HTTPRequest* request = *req;
        free((void*)request->URL);
        LinkedList_dispose(&request->headers, free_header);
        free(request);
        *req = NULL;  // ← Prevents dangling pointer
    }
}
```

**Benefits:**
- Prevents use-after-free
- Self-documenting lifetime
- Encourages immediate nullification

**Implementation Quality:** 10/10

**Recommendation:** Apply consistently everywhere
```c
// Current inconsistency:
void w_server_cleanup(w_server* server);  // ❌ Single pointer
void HTTPRequest_Dispose(HTTPRequest** request);  // ✅ Double pointer

// Should be:
void w_server_destroy(w_server** server);
```

---

### 6. Callback Pattern ✅

**Location:** Task lifecycle hooks

```c
typedef void (*mj_task_fn)(mj_scheduler* scheduler, void* ctx);

typedef struct mj_task {
    mj_task_fn run;
    mj_task_fn cleanup;
    void* ctx;
} mj_task;
```

**Benefits:**
- Inversion of control
- Scheduler doesn't know task internals
- Extensible without modifying scheduler

**Quality:** 9/10 - Well executed

---

### 7. Macro-based Iterator ⚠️

**Location:** `linked_list.h`

```c
#define LinkedList_foreach(list, node) \
    for (Node* node = (list)->head; node != NULL; node = node->front)

// Usage:
LinkedList_foreach(request->headers, node) {
    HTTPHeader* hdr = (HTTPHeader*)node->item;
    // ...
}
```

**Benefits:**
- Clean syntax
- Hides implementation details
- Type-safe iteration

**Issues:**
- No modification safety during iteration
- Variable name collision risk
- Debugging shows macro expansion

**Safety Enhancement:**
```c
// Add iteration token to detect modification
typedef struct {
    Node* current;
    size_t initial_size;
} LinkedList_iterator;

LinkedList_iterator LinkedList_iter(LinkedList* list);
Node* LinkedList_iter_next(LinkedList_iterator* iter, LinkedList* list);
```

---

## Architectural Patterns

### 1. Event Loop (Incomplete) ⚠️

**Current Implementation:**
```c
int mj_scheduler_run(mj_scheduler* scheduler) {
    while (scheduler->task_count > 0) {
        for (int i = 0; i < MAX_TASKS; i++) {
            // Run each task
        }
    }
}
```

**Pattern:** Round-robin task execution  
**Missing:** Event-driven I/O (select/epoll)

**Should Be:**
```
while (running) {
    1. Wait for I/O events (select/epoll)
    2. Process ready events
    3. Run tasks with work available
    4. Yield CPU if idle
}
```

---

### 2. Layered Architecture ✅

```
┌─────────────────────────────────┐
│     Application (main.c)        │
├─────────────────────────────────┤
│   Business Logic (w_server/     │
│                  w_client)       │
├─────────────────────────────────┤
│   Infrastructure (majjen,       │
│                  HTTPParser)     │
├─────────────────────────────────┤
│   System (POSIX sockets,        │
│           libc)                  │
└─────────────────────────────────┘
```

**Strengths:**
- Clear separation of concerns
- Testable layers
- Modular replacement

**Weaknesses:**
- HTTPParser not used (layer bypass)
- No dependency injection

---

### 3. Pipes and Filters (Absent) ❌

**Observation:** HTTP processing could benefit from pipeline:

```
Request → Parser → Validator → Router → Handler → Formatter → Response
```

**Current:** Monolithic state machine

**Recommendation:** Extract processing stages
```c
typedef struct w_pipeline {
    w_filter* filters[10];
    size_t filter_count;
} w_pipeline;

typedef int (*w_filter_fn)(w_request* req, w_response* resp);
```

---

### 4. Reactor Pattern (Partial) ⚠️

**Observed:** Non-blocking I/O with task-based event handling

**Missing Components:**
- Event demultiplexer (select/epoll)
- Event handler registration
- Event dispatch mechanism

**Target Architecture:**
```c
typedef struct mj_reactor {
    int epoll_fd;
    struct epoll_event events[MAX_EVENTS];
} mj_reactor;

typedef void (*mj_event_handler)(int fd, uint32_t events, void* ctx);

int mj_reactor_register(mj_reactor* r, int fd, 
                        uint32_t events, 
                        mj_event_handler handler, 
                        void* ctx);
```

---

## Anti-Patterns Found

### 1. God Object ⚠️

**Location:** `mj_scheduler`

**Issue:** Scheduler does too much:
- Task management
- Lifecycle control
- Execution scheduling
- Current task tracking

**Refactoring:**
```c
// Separate concerns
typedef struct mj_task_manager {
    mj_task* tasks[MAX_TASKS];
    size_t count;
} mj_task_manager;

typedef struct mj_executor {
    mj_task** current_task;
} mj_executor;

typedef struct mj_scheduler {
    mj_task_manager* tasks;
    mj_executor* executor;
    mj_reactor* reactor;
} mj_scheduler;
```

---

### 2. Magic Numbers 🔴

**Examples:**
```c
#define MAX_TASKS 5                     // Why 5?
#define W_CLIENT_READ_BUFFER_SIZE 8192  // Why 8192?
#define W_CLIENT_MAX_REQUEST_SIZE (1 * 1024 * 1024)  // Why 1MB?
#define MAX_URL_LEN 256                 // Why 256?
char address[46];                       // Why 46? (INET6_ADDRSTRLEN)
char port[6];                           // Why 6?
```

**Impact:** Hard to tune, hard to understand

**Fix:**
```c
// Document rationale
#define MAX_TASKS 5  // Current dev limit, increase to 128+ for production
#define W_CLIENT_READ_BUFFER_SIZE 8192  // Page-aligned for efficiency
#define INET6_ADDR_STRLEN 46  // RFC 4291
```

---

### 3. Busy-Wait Loop 🔴

**Location:** `mj_scheduler_run()`

```c
while (scheduler->task_count > 0) {
    for (int i = 0; i < MAX_TASKS; i++) {
        // Always executes, even with no work
    }
}
```

**Problem:** Wastes 100% CPU when idle

**Standard Solution:** Event-driven with blocking wait

---

### 4. Code Duplication 🟡

**Example:** Error printing pattern

```c
// w_server.c
fprintf(stderr, "Init failed: W_SERVER_ERROR_NO_CONFIG\n");
fprintf(stderr, "Init failed: W_SERVER_ERROR_MEMORY_ALLOCATION\n");
fprintf(stderr, "Init failed: W_SERVER_ERROR_INVALID_CONFIG\n");
```

**Refactor:**
```c
void w_server_log_error(w_server_error err) {
    fprintf(stderr, "Init failed: %s\n", w_server_error_string(err));
}
```

---

### 5. Incomplete Abstraction 🟡

**Issue:** `mj_task.create` function pointer never used

```c
typedef struct mj_task {
    mj_task_fn create;  // ← Always NULL
    mj_task_fn run;
    mj_task_fn cleanup;
    void* ctx;
} mj_task;
```

**Options:**
1. Remove unused field
2. Implement lazy initialization pattern

---

### 6. Feature Envy 🟡

**Location:** `w_server_listen_TCP_nonblocking()`

```c
void w_server_listen_TCP_nonblocking(mj_scheduler* scheduler, void* ctx) {
    w_server* server = (w_server*)ctx;
    int listen_fd = server->listen_fd;  // Reaching into server internals
    // ...
    mj_task* new_task = w_client_create(client_fd);
    mj_scheduler_task_add(scheduler, new_task);  // Reaching into scheduler
}
```

**Smell:** Function knows too much about multiple objects

**Refactor:** Delegate to proper objects

---

## Memory Management Strategy

### Current Approach: Hybrid

| Component | Strategy | Notes |
|-----------|----------|-------|
| Scheduler | Stack/Static | Lives in main() |
| Tasks | Heap (caller-owned) | Freed by scheduler |
| Task Context | Heap (task-owned) | Freed by cleanup callback |
| Buffers | Mixed | Some stack, some heap |

### Memory Ownership Rules (Implicit)

1. **Scheduler owns tasks**: Frees in `mj_scheduler_task_remove_current()`
2. **Tasks own context**: Freed via cleanup callback
3. **Context owns buffers**: Manually freed in cleanup
4. **Strings**: Sometimes duplicated (`strdup`), sometimes borrowed

### Issues Found

#### 1. Inconsistent Ownership
```c
// w_client_create() - caller owns result
mj_task* task = w_client_create(fd);

// HTTPRequest_fromstring() - caller owns result
HTTPRequest* req = HTTPRequest_fromstring(msg);
```
**Problem:** No way to know without reading implementation

**Solution:** Document in header
```c
/**
 * Creates a client task
 * @return Heap-allocated task, ownership transferred to caller
 *         Must be freed via scheduler or mj_task_destroy()
 */
mj_task* w_client_create(int client_fd);
```

#### 2. Cleanup Fragility
```c
// If allocation fails mid-create:
mj_task* new_task = calloc(1, sizeof(*new_task));
if (new_task == NULL) {
    return NULL;  // ❌ Leaks previously allocated context
}
w_client* new_ctx = calloc(1, sizeof(*new_ctx));
if (new_ctx == NULL) {
    return NULL;  // ❌ Leaks task
}
```

**Fix:** Use goto cleanup pattern
```c
mj_task* w_client_create(int client_fd) {
    mj_task* task = NULL;
    w_client* ctx = NULL;
    
    task = calloc(1, sizeof(*task));
    if (!task) goto error;
    
    ctx = calloc(1, sizeof(*ctx));
    if (!ctx) goto error;
    
    // ... initialize ...
    return task;
    
error:
    free(ctx);
    free(task);
    return NULL;
}
```

---

## Concurrency Model

### Current: Single-Threaded Cooperative

```
┌─────────────────────────────────┐
│       Single Thread             │
│                                 │
│  ┌──────┐ ┌──────┐ ┌──────┐    │
│  │Task 1│ │Task 2│ │Task 3│    │
│  └──┬───┘ └──┬───┘ └──┬───┘    │
│     │        │        │         │
│     └────────┴────────┘         │
│     (cooperatively yield)       │
└─────────────────────────────────┘
```

**Characteristics:**
- No threading overhead
- No synchronization needed
- No true parallelism
- Blocking task stalls everything

### Future: Event-Driven Cooperative

```
┌─────────────────────────────────┐
│   Event Loop (select/epoll)     │
└─────────────┬───────────────────┘
              │
    ┌─────────┴─────────┐
    │                   │
┌───▼────┐         ┌───▼────┐
│ Ready  │         │ Ready  │
│ Task 1 │         │ Task 2 │
└────────┘         └────────┘
```

### Scaling Path

1. **Current:** Single-threaded cooperative (MVP)
2. **Phase 2:** Event-driven cooperative (production)
3. **Phase 3:** Thread pool for CPU-bound tasks
4. **Phase 4:** Multi-process with shared-nothing

---

## Extension Points

### 1. Middleware System (Prepared)

**Evidence:**
- Empty `src/middleware/` directory
- Modular task structure

**Recommended Implementation:**
```c
typedef int (*middleware_fn)(w_request* req, w_response* resp, 
                             void* next);

typedef struct w_middleware {
    const char* name;
    middleware_fn handler;
    void* config;
} w_middleware;

// Example middleware:
int cors_middleware(w_request* req, w_response* resp, void* next);
int auth_middleware(w_request* req, w_response* resp, void* next);
int logging_middleware(w_request* req, w_response* resp, void* next);
```

---

### 2. Pluggable Listeners

**Code Comment Identified:**
> "TODO: w_server_listen_TCP_nonblocking() should be a separate module"

**Design:**
```c
typedef struct w_listener {
    const char* name;
    int (*init)(w_server* server, w_server_config* config);
    void (*accept)(mj_scheduler* scheduler, void* ctx);
    void (*cleanup)(w_server* server);
} w_listener;

// Implementations:
extern w_listener tcp_nonblocking_listener;
extern w_listener tcp_blocking_listener;
extern w_listener tls_listener;
extern w_listener unix_socket_listener;
```

---

### 3. Protocol Handlers

**Current:** HTTP hardcoded

**Extensible Design:**
```c
typedef struct w_protocol {
    const char* name;
    int (*parse)(const char* data, size_t len, void** result);
    int (*format)(void* data, char** output, size_t* len);
    void (*free)(void* data);
} w_protocol;

// Register protocols:
w_server_register_protocol(server, &http_protocol);
w_server_register_protocol(server, &websocket_protocol);
w_server_register_protocol(server, &grpc_protocol);
```

---

### 4. Storage Backends

**Not Yet Present, But Natural Extension:**
```c
typedef struct w_storage {
    int (*get)(const char* key, char** value);
    int (*set)(const char* key, const char* value);
    int (*del)(const char* key);
} w_storage;

// Implementations:
extern w_storage memory_storage;
extern w_storage redis_storage;
extern w_storage sqlite_storage;
```

---

## Recommendations Summary

### Pattern Adoption

| Pattern | Priority | Effort | Benefit |
|---------|----------|--------|---------|
| Event-driven reactor | 🔴 Critical | High | Fixes CPU usage |
| Proper object pool | 🟡 High | Medium | Scalability |
| Middleware chain | 🟢 Medium | Medium | Extensibility |
| Pluggable listeners | 🟢 Medium | Low | Modularity |
| Protocol abstraction | 🔵 Low | High | Future-proofing |

### Anti-Pattern Fixes

1. ✅ **Keep:** State pattern, dispose pattern, callback pattern
2. 🔄 **Refactor:** Busy-wait loop, fixed task array, god object
3. 🗑️ **Remove:** Unused `create` callback, magic numbers, debug sleeps

### Design Principles Adherence

| Principle | Score | Notes |
|-----------|-------|-------|
| **Single Responsibility** | 7/10 | Mostly good, scheduler does too much |
| **Open/Closed** | 6/10 | Extension points exist but unused |
| **Liskov Substitution** | N/A | No inheritance in C |
| **Interface Segregation** | 8/10 | Focused interfaces |
| **Dependency Inversion** | 5/10 | Tight coupling to implementation |
| **DRY** | 6/10 | Some duplication in error handling |
| **KISS** | 9/10 | Simple, understandable code |
| **YAGNI** | 7/10 | Some unused features (create callback) |

---

## Conclusion

The codebase demonstrates **solid grasp of fundamental patterns** appropriate for C systems programming. The state machine, callback-based task system, and memory management patterns are well-executed.

**Key Strengths:**
- Clean separation of concerns
- Appropriate pattern selection for C
- Consistent coding style
- Good foundation for extension

**Key Weaknesses:**
- Incomplete implementations leave patterns half-realized
- Missing critical pattern (event-driven I/O)
- Some anti-patterns (busy-wait, magic numbers)
- Inconsistent ownership semantics

**Overall Grade: B (Good foundation, needs completion)**

With focused effort on completing the HTTP integration, implementing event-driven I/O, and fixing the identified anti-patterns, this could easily become an A-grade architecture.

---

**End of Analysis**

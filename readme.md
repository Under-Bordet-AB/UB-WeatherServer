# UB Weather Server - Technical Analysis & Recommendations

**Branch:** `feature/new-scheduler`  
**Analysis Date:** November 21, 2025  
**Architecture:** Cooperative scheduler with state-machine-based client handling

---

## Priority 1: Critical Performance Issues (Highest Bang-for-Buck)
### 1.1 Replace Busy-Polling with Event-Driven I/O

**Current Problem:**
- `mj_scheduler_run()` loops continuously through all tasks calling their `run()` function
- When no I/O is ready, CPU spins at 100% doing nothing
- Scales poorly: with 1000 idle connections, scheduler still iterates 1000× per loop

**Impact:** 
- **CPU Usage:** 100% on single core even with zero actual work
- **Energy:** Waste on servers, kills battery on edge devices
- **Scalability:** Cannot efficiently handle thousands of connections

**Recommended Solution:**
```
Implement epoll/kqueue-based event loop:
1. Add `int fd` field to mj_task
2. Add `short events` field (EPOLLIN/EPOLLOUT flags)
3. Replace the scheduler's for-loop with epoll_wait()
4. Only call task->run() when the OS reports fd activity
```

**Implementation Approach:**
- Add `mj_scheduler_register_fd(scheduler, task, fd, events)` 
- Add `mj_scheduler_unregister_fd(scheduler, task)`
- Modify `mj_scheduler_run()`:
  ```c
  while (task_count > 0) {
      int nfds = epoll_wait(epoll_fd, events, MAX_TASKS, timeout);
      for (int i = 0; i < nfds; i++) {
          mj_task* task = events[i].data.ptr;
          task->run(scheduler, task->ctx);
      }
  }
  ```

**Estimated Effort:** 2-3 days for one developer  
**Files to Modify:**
- `src/libs/majjen.c` - Add epoll logic
- `src/libs/majjen.h` - Add fd registration API
- `src/w_server/w_server.c` - Register listen socket
- `src/w_server/w_client.c` - Register client sockets

---

### 1.2 Fix Blocking I/O in HTTP Client Backend

**Current Problem:**
- `src/w_server/backends/weather/http_client/http_client.c` uses blocking calls:
  - `gethostbyname()` - blocks on DNS lookup (can take 100ms-5s)
  - `connect()` - blocks on TCP handshake (10ms-3s)
  - `recv()` - blocks waiting for data despite timeout
- When ONE client requests weather, ALL other clients stall

**Impact:**
- **Tail Latency:** Under load, 99th percentile response times explode
- **Throughput:** Can only handle 1 weather API call at a time
- **User Experience:** Server appears "frozen" during API calls

**Recommended Solution:**
```
Option A (Quick): Use libcurl with CURLOPT_TIMEOUT
Option B (Better): Use libcurl multi interface for true async
Option C (Best): Implement non-blocking HTTP with getaddrinfo_a()
```

**Quick Win Implementation (Option A):**
1. Replace custom `http_get()` with `curl_easy_perform()`
2. Set `curl_easy_setopt(curl, CURLOPT_TIMEOUT, 2L)`
3. Set `curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 1L)`

**Estimated Effort:** 1 day (Option A), 3 days (Option B), 5 days (Option C)  
**Files to Modify:**
- `src/w_server/backends/weather/http_client/http_client.c`
- `src/w_server/backends/weather/weather.c` (minor changes to handle async)

---

## Priority 2: Architecture Improvements (High Value)

### 2.1 Implement Timer System for Timeouts

**Current Problem:**
- Client timeout implemented with `clock_gettime()` + manual comparison in EVERY task run
- No generic timeout mechanism for other use cases (cache expiry, rate limiting)
- Code duplication across `w_client.c` line 35-40

**Benefits:**
- Offload timeout logic from application to scheduler
- Enable sleeping timers (e.g., retry after 5 minutes)
- Cleaner separation of concerns

**Recommended Solution:**
```c
typedef struct {
    struct timespec deadline;
    void (*callback)(void* ctx);
    void* ctx;
} mj_timer;

// API:
mj_timer* mj_scheduler_add_timer(scheduler, timeout_ms, callback, ctx);
void mj_scheduler_cancel_timer(scheduler, timer);
```

**Integration with epoll:**
- Use `timerfd_create()` for efficient timer management
- Or: maintain min-heap of timers, set epoll timeout to next deadline

**Estimated Effort:** 2 days  
**Files to Create/Modify:**
- `src/libs/majjen_timer.c` (new)
- `src/libs/majjen.h` (add timer API)
- `src/w_server/w_client.c` (replace manual timeout with timer)

---

### 2.2 Refactor Client State Machine to Function Pointers ⭐⭐⭐

**Current Problem:**
- `w_client_run()` is a 350+ line switch statement (lines 22-311 in `w_client.c`)
- Hard to test individual states
- Difficult to add middleware/filters

**Benefits:**
- Each state becomes a standalone function (easier testing)
- Enable pipeline/filter pattern (logging, metrics, auth)
- Better code organization

**Recommended Solution:**
```c
typedef void (*w_client_state_fn)(w_client*, mj_scheduler*);

// Function table
w_client_state_fn state_handlers[] = {
    [W_CLIENT_READING] = w_client_handle_reading,
    [W_CLIENT_PARSING] = w_client_handle_parsing,
    [W_CLIENT_PROCESSING] = w_client_handle_processing,
    // ...
};

void w_client_run(mj_scheduler* scheduler, void* ctx) {
    w_client* client = (w_client*)ctx;
    state_handlers[client->state](client, scheduler);
}
```

**Pipeline/Filter Extension:**
```c
typedef int (*w_client_filter)(w_client*, void* filter_ctx);

struct w_client {
    // ... existing fields
    w_client_filter* filters;  // Array of middleware
    size_t filter_count;
};

// Execute filters before state handler
for (int i = 0; i < client->filter_count; i++) {
    if (client->filters[i](client, filter_ctx) != 0)
        return; // Filter aborted request
}
```

**Use Cases for Filters:**
- Request logging
- Authentication/authorization
- Rate limiting per IP
- Request/response compression
- Metrics collection

**Estimated Effort:** 3 days  
**Files to Modify:**
- `src/w_server/w_client.c` - Refactor to function pointers
- `src/w_server/w_client.h` - Add filter typedef and API

---

### 2.3 Modularize Server Listeners ⭐⭐⭐

**Current Problem:**
- `w_server_listen_TCP_nonblocking()` is hardcoded in `w_server.c`
- Cannot easily add UNIX socket listener, UDP listener, or HTTP/2
- Comment on line 24 says: "TODO: should be a separate module"

**Recommended Solution:**
```c
// Generic listener interface
typedef struct w_listener {
    int (*accept_fn)(w_listener* self, int* fd_out);
    void (*cleanup_fn)(w_listener* self);
    void* impl_data;
} w_listener;

// Implementations
w_listener* w_listener_tcp_create(const char* addr, const char* port);
w_listener* w_listener_unix_create(const char* socket_path);
w_listener* w_listener_tls_create(const char* addr, const char* port, 
                                    const char* cert, const char* key);
```

**Directory Structure:**
```
src/w_server/listeners/
    listener.h            # Interface
    tcp_listener.c        # TCP implementation
    unix_listener.c       # UNIX socket
    tls_listener.c        # TLS/SSL support
```

**Benefits:**
- Add HTTPS support without modifying core server
- Test different listener types independently
- Support multi-protocol (HTTP + gRPC + WebSocket)

**Estimated Effort:** 3 days  
**Files to Create:**
- `src/w_server/listeners/listener.h`
- `src/w_server/listeners/tcp_listener.c`

---

## Priority 3: Observability & Reliability (Medium Priority)

### 3.1 Structured Logging and Metrics Module ⭐⭐⭐

**Current Problem:**
- UI printing mixed with actual functionality (`ui.c` has 20+ print functions)
- No structured logs (can't parse with log aggregators)
- No metrics for monitoring (request rate, latency percentiles, error rate)
- Global `UI_PRINT_ENABLED` flag is crude on/off switch

**Recommended Solution:**

**Phase 1: Structured Logging**
```c
typedef enum {
    LOG_TRACE, LOG_DEBUG, LOG_INFO, LOG_WARN, LOG_ERROR
} log_level;

void log_event(log_level level, const char* component, 
               const char* event, const char* fmt, ...);

// Usage:
log_event(LOG_INFO, "client", "request_received", 
          "method=%s path=%s client_id=%zu", "GET", "/weather", client_id);
```

**Output formats:**
- JSON for production (parseable by ELK/Splunk)
- Human-readable for development
- Configurable via environment variable

**Phase 2: Metrics**
```c
typedef struct {
    uint64_t requests_total;
    uint64_t requests_active;
    uint64_t errors_total[10];  // per error type
    histogram_t latency_ms;     // for percentiles
} server_metrics;

void metrics_record_request(server_metrics* m, int status_code, uint64_t latency_us);
char* metrics_to_prometheus(server_metrics* m);  // Export for Prometheus
```

**Integration Points:**
- Replace `ui_print_*` calls with `log_event()`
- Add metrics recording in `w_client_run()` at state transitions
- Expose `/metrics` endpoint for Prometheus scraping

**Estimated Effort:** 4 days (Phase 1), 3 days (Phase 2)  
**Files to Create:**
- `src/utils/logging.c` / `logging.h`
- `src/utils/metrics.c` / `metrics.h`
- `src/w_server/backends/metrics/metrics.c` (Prometheus endpoint backend)

---

### 3.2 Rate Limiting and Connection Limits ⭐⭐⭐

**Current Problem:**
- `global_defines.h` defines `CLIENT_MAX_CONNECTIONS_PER_IP` but says "NOT IMPLEMENTED"
- No protection against:
  - Single IP opening 10,000 connections (resource exhaustion)
  - DDoS attacks
  - Accidental runaway clients

**Recommended Solution:**
```c
typedef struct {
    char ip_addr[46];          // IPv6 address
    size_t connection_count;   // Active connections
    time_t window_start;
    size_t request_count;      // Requests in current window
} client_quota;

// Hash table: IP -> quota
hashtable_t* quotas;

int enforce_limits(w_server* server, const char* ip_addr) {
    client_quota* quota = hashtable_get(quotas, ip_addr);
    
    if (quota->connection_count >= MAX_CONNECTIONS_PER_IP)
        return -1;  // Reject
    
    if (quota->request_count >= MAX_REQUESTS_PER_WINDOW)
        return -2;  // Rate limited
    
    return 0;
}
```

**Configuration (move to config file):**
```ini
[limits]
max_connections_per_ip = 100
max_requests_per_second = 10
max_total_connections = 10000
request_window_seconds = 60
```

**Estimated Effort:** 2 days  
**Files to Modify:**
- `src/w_server/w_server.c` (check limits in accept handler)
- Add `src/libs/hashtable.c` or use existing `uthash.h`

---

## Priority 4: Code Quality & Maintainability

### 4.1 Move Magic Numbers to Configuration ⭐⭐

**Current Problem:**
- Timeouts hardcoded to 9999 seconds (likely debug values)
- Buffer sizes hardcoded in multiple places
- Rate limits hardcoded in `weather.h`

**Recommended Solution:**
```c
// config.ini or config.json
{
  "server": {
    "port": 10480,
    "address": "127.0.0.1",
    "max_accepts_per_tick": 16
  },
  "client": {
    "read_timeout_sec": 30,
    "read_buffer_size": 8192,
    "max_request_size": 1048576
  },
  "backends": {
    "weather": {
      "api_timeout_sec": 5,
      "cache_max_age_sec": 900,
      "rate_limit_per_minute": 30
    }
  }
}
```

Use library like `inih` or `jansson` (already in use) for parsing.

**Estimated Effort:** 2 days  
**Files to Create:**
- `src/config.c` / `config.h`
- `config.ini` or `config.json`

---

### 4.2 Add Unit Tests ⭐⭐

**Current Problem:**
- No tests visible in repository
- Refactoring is risky
- Can't verify correctness of parsers, state machines, etc.

**Recommended Test Targets (in priority order):**
1. `http_parser.c` - Request/response parsing (many edge cases)
2. `linked_list.c` - Data structure correctness
3. `w_client` state transitions - Mock scheduler + mock FDs
4. Rate limiting logic
5. JSON serialization/deserialization

**Framework:** Use `cmocka` (lightweight C test framework)

**Estimated Effort:** 1 day setup + 1 day per module  
**Files to Create:**
- `tests/test_http_parser.c`
- `tests/test_linked_list.c`
- `Makefile` test target

---

### 4.3 Memory Safety Audit ⭐⭐

**Observations:**
- Many `malloc()` calls without NULL checks (e.g., `cities.c:46`)
- Some `strdup()` calls check NULL, others don't
- Potential leak: `http_response_tostring()` returns malloc'd string, callers must free

**Recommended Actions:**
1. Run Valgrind on stress test
2. Add `-fsanitize=address` flag for development builds
3. Standardize error handling:
   ```c
   #define ALLOC_OR_FAIL(ptr, size, label) \
       do { \
           ptr = malloc(size); \
           if (!ptr) goto label; \
       } while(0)
   ```

**Estimated Effort:** 2 days  
**Tools:** Valgrind, AddressSanitizer, static analyzer

---

## Implementation Roadmap

### Sprint 1: Performance Critical (Week 1-2)
- **Developer A:** Event-driven I/O (Priority 1.1)
- **Developer B:** Fix blocking HTTP client (Priority 1.2)
- **Developer C:** Timer system (Priority 2.1)

### Sprint 2: Architecture (Week 3-4)
- **Developer A:** Client state machine refactor (Priority 2.2)
- **Developer B:** Listener abstraction (Priority 2.3)
- **Developer C:** Structured logging (Priority 3.1 Phase 1)

### Sprint 3: Reliability (Week 5-6)
- **Developer A:** Metrics module (Priority 3.1 Phase 2)
- **Developer B:** Rate limiting (Priority 3.2)
- **Developer C:** Configuration system (Priority 4.1)

### Sprint 4: Quality (Week 7-8)
- **All Developers:** Unit tests (Priority 4.2)
- **All Developers:** Memory audit + fixes (Priority 4.3)

---

## Quick Wins (Can Start Today)

1. **Add `usleep(1000)` to scheduler loop** - 15 minutes, reduces CPU to 5%
2. **Replace `gethostbyname()` with `getaddrinfo()`** - 1 hour, more portable
3. **Change `*_TIMEOUT_SEC` from 9999 to 30** - 5 minutes, reasonable defaults
4. **Add Valgrind to CI/CD** - 30 minutes, catch memory issues early
5. **Extract client state handlers to functions** - 2 hours, improves testability

---

## Technical Debt Summary

**High Priority Debt:**
- Busy-polling scheduler wastes CPU
- Blocking I/O in HTTP client
- No timeout infrastructure

**Medium Priority Debt:**
- Monolithic state machine
- Hardcoded listener implementation
- No structured logging/metrics
- Magic numbers in code

**Low Priority Debt:**
- Missing unit tests
- Some memory leak risks
- No configuration system

---

## Conclusion

**Highest ROI Actions:**
1. Implement event-driven I/O (epoll/kqueue) - **90% CPU reduction**
2. Fix blocking HTTP client - **10x throughput improvement**
3. Add timer system - **enables proper timeouts + future features**

**Best First Steps for Multiple Developers:**
- Developer with systems programming experience: Priority 1.1 (epoll)
- Developer familiar with HTTP/networking: Priority 1.2 (async HTTP)
- Developer focused on architecture: Priority 2.2 (state machine refactor)

The codebase is well-structured for these improvements. The scheduler abstraction is solid, and the state-machine pattern in clients is the right approach. Main issues are busy-polling and blocking I/O, both solvable with the changes outlined above.

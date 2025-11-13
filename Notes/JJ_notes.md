# Practical C Commenting and HTTP Buffering Guide
A fact-checked, opinionated guide for C developers building POSIX socket servers. This document combines style guidance (how to comment C code effectively) with a design reference for HTTP request buffering, limits, and memory placement.

Use it as:
- A style guide for comments and API documentation
- A design reference for HTTP buffering (headers and bodies)
- A checklist for security, performance, and correctness

----------------------------------------------------------------

## TL;DR Recommendations

- Commenting
  - Document the why and the contract (preconditions, postconditions, ownership), not the obvious what.
  - Use Doxygen-style comments for public APIs and non-trivial functions.
  - Document invariants, error semantics, thread-safety, units, and lifetime.

- HTTP header buffering
  - There is no RFC-mandated limit. Use a configurable total header cap with sane defaults.
  - Start with an 8 KiB initial read buffer for headers; grow dynamically up to a configured cap (e.g., 16–32 KiB).
  - On overflow, reject with 431 Request Header Fields Too Large (or 400 for malformed headers).

- HTTP body buffering
  - Use a small read chunk buffer (4–16 KiB).
  - If Content-Length is present, enforce a configured max body size; pre-allocate heap buffer if you must store fully.
  - If Transfer-Encoding: chunked, stream/append incrementally; enforce per-chunk and total size caps; terminate on chunk-size 0.

- Memory placement
  - Use stack buffers for small, short-lived, per-connection operations (e.g., initial header read).
  - Use heap for large or long-lived buffers (e.g., full body, persistent per-connection state).
  - Consider a buffer pool/arena only after you have measured a bottleneck.

- Safety and robustness
  - Enforce read timeouts (Slowloris defense), header count/line length limits, and maximum body sizes.
  - Validate Content-Length, guard against integer overflow/underflow, and handle mixed Content-Length + chunked as 400 Bad Request.
  - Treat all socket I/O as partial; loop until done or error.

----------------------------------------------------------------

## 1. Commenting C Code Effectively

Goal: Write comments that reduce cognitive load for maintainers without duplicating what the code already states.

### 1.1 File and Header Comments (The Big Picture)
- Header (`.h`): API description for consumers of the interface.
- Source (`.c`): Implementation overview, dependencies, key design notes.

Example (Doxygen style):
```c
/** @file socket_handler.h
 *  @brief Public API for POSIX socket creation and I/O helpers.
 *
 *  Provides socket lifecycle helpers and I/O primitives with timeouts.
 *  Thread-safe unless documented otherwise.
 */
```

```c
/* socket_handler.c
 * Implementation of the socket and I/O helpers.
 * Depends on <sys/socket.h>, <netinet/in.h>, <poll.h>, and error_handler.h.
 * Notes: Uses non-blocking I/O and poll() for timeouts.
 */
```

Include:
- Purpose and scope of the file
- External dependencies and assumptions
- Non-obvious design choices and trade-offs

### 1.2 Function Comments (The Contract)
Document non-trivial and all public functions with:
- What the function does (brief)
- Preconditions/postconditions
- Ownership and lifetime rules
- Thread-safety and blocking behavior
- Error semantics

Example:
```c
/**
 * @brief Sends an HTTP response and closes the connection.
 * @param connection_fd Connected client file descriptor (non-negative).
 * @param response_ptr  Pointer to complete response buffer to send.
 *                      Ownership remains with caller; buffer must remain
 *                      valid until function returns.
 * @param response_len  Number of bytes in response_ptr to send.
 * @return 0 on success; -1 on I/O error (errno set).
 * @note Blocks until send completes or fails. Not thread-safe per fd.
 */
int HTTP_SendAndClose(int connection_fd, const char *response_ptr, size_t response_len);
```

Recommended tags: @brief, @param, @return, @note, @warning, @pre, @post, @retval

### 1.3 Inline Comments (The Why)
Use line comments to explain:
- Non-obvious logic, invariants, and constraints
- Intentional workarounds and their rationale (include links to bug IDs)
- Performance-sensitive choices and expected complexity
- TODO/FIXME with context and a measurable intention

Examples:
```c
// Use right-shift for power-of-two division (hot path): ~30% faster in microbench.
value >>= 3;

/* Magic number: 8 KiB aligns with typical HTTP header buffer defaults (configurable). */
#define HEADER_BUF_DEFAULT 8192

// TODO(jane): Replace usleep with timerfd for precise scheduling under load.
```

Avoid:
- Describing the obvious
- Commenting variable initializations that can be self-describing with better names
- Keeping commented-out code (use version control)

### 1.4 Documenting Types and Macros
- Document struct fields, units, and ownership.
- For macros, document side effects, evaluation cost, and safe usage patterns.

```c
/** Connection state for a single client (owned by worker thread). */
struct client_ctx {
    int fd;                 /**< Socket file descriptor. */
    char *body;             /**< Heap-allocated request body; may be NULL. */
    size_t body_len;        /**< Bytes in body; 0 if none. */
    bool keep_alive;        /**< HTTP keep-alive requested and permitted. */
    // ...
};
```

----------------------------------------------------------------

## 2. HTTP Buffering: Facts and Design Choices

### 2.1 There Is No RFC-Mandated Header Size Limit
- RFC 7230/9110 do not specify a hard limit for HTTP header size.
- Servers and proxies impose their own limits for safety.
  - NGINX: defaults commonly align with 8 KiB per-buffer; configurable via [large_client_header_buffers](https://nginx.org/en/docs/http/ngx_http_core_module.html#large_client_header_buffers).
  - Apache HTTPD: header size and line limits configurable (e.g., [LimitRequestFieldSize](https://httpd.apache.org/docs/current/mod/core.html#limitrequestfieldsize), [LimitRequestLine](https://httpd.apache.org/docs/current/mod/core.html#limitrequestline)); defaults often around 8 KiB per field/line.
  - IIS/HTTP.sys: request limits include [MaxRequestBytes and MaxFieldLength](https://learn.microsoft.com/iis/configuration/system.webserver/security/requestfiltering/requestlimits/); defaults historically around 16 KiB.
  - HAProxy: buffer size tunable via [tune.bufsize](https://www.haproxy.org/download/2.9/doc/configuration.txt); default commonly 16 KiB.

Practical conclusion:
- Default to 8–16 KiB for total header acceptance per request.
- Make it configurable; pick a hard cap where you will reject the request cleanly (431).

### 2.2 Reading Headers Robustly
- You must detect the end of headers (CRLF CRLF).
- Socket reads can return partial data; loop until terminator is found or limits/timeouts are reached.
- Strategy:
  1. Use a stack buffer (e.g., 8 KiB) for the initial read.
  2. If CRLFCRLF not found and you hit the buffer end, grow into a heap buffer up to a configured maximum (e.g., 16–32 KiB).
  3. On exceeding the cap or timeout, respond with 431 or 400 and close.

Pseudo-code:
```c
char header_stack[8192];
size_t used = 0;
char *hdr = header_stack;
size_t cap = sizeof(header_stack);
bool using_heap = false;

while (true) {
    ssize_t n = recv(fd, hdr + used, cap - used, 0);
    if (n < 0) { if (errno == EAGAIN) continue; else goto bad_io; }
    if (n == 0) goto client_closed;
    used += (size_t)n;

    // Search for "\r\n\r\n"
    if (used >= 4 && find_double_crlf(hdr, used)) break;

    // Need more space?
    if (used == cap) {
        if (cap >= MAX_HEADER_BYTES) goto too_large;
        size_t new_cap = min(cap * 2, MAX_HEADER_BYTES);
        char *new_hdr = using_heap ? realloc(hdr, new_cap) : malloc(new_cap);
        if (!new_hdr) goto oom;
        if (!using_heap) memcpy(new_hdr, header_stack, used);
        hdr = new_hdr; cap = new_cap; using_heap = true;
    }
}
```

Return codes:
- 431 Request Header Fields Too Large: header exceeds configured limits
- 400 Bad Request: malformed headers, invalid line folding, illegal characters
- 408 Request Timeout: header read exceeded timeout

Related considerations:
- Enforce max header count (e.g., 100–200)
- Enforce max header line length
- Normalize and validate header names/values
- Respect Connection, Host, Content-Length, Transfer-Encoding semantics

### 2.3 Body Reading Strategies

A. Content-Length present:
- Validate it: parse as unsigned, check against a configured maximum.
- Guard against integer overflow (e.g., when adding offsets).
- If you need the full body in memory, pre-allocate on heap and read in chunks.
- Consider streaming to file for large uploads; avoid massive in-memory buffers.

B. Transfer-Encoding: chunked:
- Parse hexadecimal chunk sizes; reject negatives, overflow, or absurdly large chunks.
- Loop: [size][CRLF][data][CRLF] until size=0 then optional trailers.
- Enforce per-chunk and total size caps; enforce trailer limits or disable trailers.
- Reject ambiguous or illegal combinations (e.g., both Content-Length and chunked).

C. Unknown/unsupported:
- If neither Content-Length nor chunked is present for methods that may carry bodies, some servers assume no body; be strict and spec-compliant for your use case.

HTTP status and errors:
- 413 Payload Too Large: body exceeds configured limit
- 400 Bad Request: malformed chunking or illegal header combinations

### 2.4 Keep-Alive, Pipelining, and Reuse
- Keep-Alive: reuse the same connection for multiple requests. Reuse buffers where possible but reset state.
- HTTP/1.1 pipelining: complex and widely discouraged; consider disabling.
- For persistent connections, prefer reusing a header buffer (stack or per-connection heap) sized to the most recent peak usage (capped).

### 2.5 HTTP/2 and HTTP/3 Notes
- HTTP/2 and HTTP/3 use frames, not CRLF CRLF, and impose different flow-control and header compression (HPACK/QPACK) concerns.
- This guide focuses on HTTP/1.1. If supporting HTTP/2+, adopt a library or plan for:
  - Frame parsing, flow control windows
  - Header compression contexts
  - Different buffering policies and back-pressure

----------------------------------------------------------------

## 3. Memory Placement: Stack vs. Heap vs. Pools

### 3.1 Stack Allocation (per-connection, short-lived)
- Ideal for initial header buffer (e.g., 8 KiB) and small chunk read buffers (4–16 KiB).
- Pros: fast, thread-local, automatic cleanup.
- Cons: limited size; data dies on return; avoid large allocations (risk of stack overflow).

Example:
```c
char header_buf[8192];
ssize_t n = recv(fd, header_buf, sizeof(header_buf), 0);
```

### 3.2 Heap Allocation (long-lived or large)
- Use for bodies, persistent per-connection state, or grown headers beyond the stack.
- Pros: dynamic, persistent; can be passed across layers/threads (with care).
- Cons: allocation cost; must free; risk of fragmentation and leaks.

Example with bounds checking:
```c
size_t content_len = parse_content_length(hdr);
if (content_len > max_body_bytes) return http_413();
char *body = malloc(content_len);
if (!body) return http_500();
```

### 3.3 Pools/Arenas and Thread-Local Caches (advanced)
- Pre-allocate fixed-size buffers or use monotonic arenas for predictable performance.
- Useful in high-throughput, event-driven servers (epoll/kqueue).
- Complexity: requires lifecycle discipline; add metrics and leak detection before adopting.

----------------------------------------------------------------

## 4. Error Handling, Timeouts, and Security

- Timeouts
  - Header read timeout (e.g., 5–10s) to mitigate Slowloris
  - Body inactivity timeout (e.g., 30–60s) and overall request timeout
  - Use poll/epoll/select or SO_RCVTIMEO for read timeouts

- Limits
  - Max total header bytes (e.g., 16 KiB)
  - Max header fields (e.g., 100–200)
  - Max header line length (e.g., 8 KiB)
  - Max body bytes (e.g., 8 MiB for APIs; higher for uploads; configuration-driven)
  - Max chunk size and total chunked bytes

- Validation
  - Reject invalid characters and obsolete line folding in headers
  - Reject both Content-Length and chunked
  - Validate Host presence for HTTP/1.1
  - Normalize Connection semantics

- Integer safety
  - Use size_t/ssize_t for sizes; check arithmetic overflow before allocation
  - Use strtoull with endptr checks; guard against values > SIZE_MAX

- Concurrency
  - Do not share connection buffers across threads without synchronization
  - Define and document ownership: who frees what and when

- Logging and observability
  - Log reason for rejections with safe truncation and redaction
  - Track rates for limit hits, timeouts, malformed requests

----------------------------------------------------------------

## 5. Practical Code Patterns

### 5.1 Configurable Limits
```c
enum { DEFAULT_MAX_HEADER_BYTES = 16 * 1024 };
enum { DEFAULT_MAX_BODY_BYTES   = 8  * 1024 * 1024 };
enum { READ_CHUNK_BYTES         = 8  * 1024 };
```

### 5.2 Read Loop with Partial Reads
```c
ssize_t read_full(int fd, void *buf, size_t want, int timeout_ms);
ssize_t write_full(int fd, const void *buf, size_t len, int timeout_ms);
```
- Implement with poll()/epoll and handle EINTR/EAGAIN.

### 5.3 Robust Content-Length Parsing
```c
bool parse_content_length(const char *s, size_t *out) {
    errno = 0;
    char *end = NULL;
    unsigned long long v = strtoull(s, &end, 10);
    if (errno || end == s || *end != '\0' || v > SIZE_MAX) return false;
    *out = (size_t)v;
    return true;
}
```

### 5.4 Chunked Encoding Outline
- State machine: READ_SIZE -> READ_DATA -> READ_CRLF -> DONE
- Enforce limits; handle chunk extensions (ignore or limit length)

----------------------------------------------------------------

## 6. Testing and Hardening Checklist

- Headers
  - > cap total header size (expect 431)
  - Many small headers (count limit)
  - Single very long header line (line-length limit)
  - Invalid characters, missing CRLFCRLF (400)

- Bodies
  - Content-Length matches/mismatches actual data
  - Content-Length with no body
  - Chunked: malformed size, overflow, missing CRLF, trailers too large
  - Combined Content-Length + chunked (400)
  - Over max body (413)

- Network behavior
  - Slowloris: drip headers slowly
  - Early close during body
  - Keep-alive multiple requests; buffer reuse; state reset
  - Non-blocking with EAGAIN/EINTR chaos testing

- Memory
  - ASan/UBSan, leak checks
  - Fuzz header parser (e.g., libFuzzer/AFL)

----------------------------------------------------------------

## 7. Performance Notes

- Use sendfile/splice where applicable for zero-copy responses
- Tune socket buffers (SO_RCVBUF/SO_SNDBUF), Nagle (TCP_NODELAY) based on workload
- Avoid frequent malloc/free in hot paths; reuse buffers when safe
- Measure before optimizing; add latency histograms and counters

----------------------------------------------------------------

## 8. References and Further Reading

- HTTP Semantics (RFC 9110): [RFC 9110](https://www.rfc-editor.org/rfc/rfc9110)
- HTTP/1.1 Message Syntax and Routing (historic, superseded by 9110): [RFC 7230](https://www.rfc-editor.org/rfc/rfc7230)
- Apache HTTP Server limits: [LimitRequestFieldSize](https://httpd.apache.org/docs/current/mod/core.html#limitrequestfieldsize), [LimitRequestLine](https://httpd.apache.org/docs/current/mod/core.html#limitrequestline)
- NGINX header buffer config: [large_client_header_buffers](https://nginx.org/en/docs/http/ngx_http_core_module.html#large_client_header_buffers)
- IIS/HTTP.sys request limits: [requestLimits (IIS)](https://learn.microsoft.com/iis/configuration/system.webserver/security/requestfiltering/requestlimits/)
- HAProxy buffer size: [tune.bufsize](https://www.haproxy.org/download/2.9/doc/configuration.txt)

----------------------------------------------------------------

## 9. Rationale for Key Choices (Fact Check Notes)

- 8 KiB header buffers are common defaults but not mandated by standards. Servers differ: NGINX often ~8 KiB per buffer (configurable), Apache and IIS defaults vary (commonly 8–16 KiB). Therefore: make it configurable and cap total accepted header size.
- Body buffer size “standards” do not exist. The 4–16 KiB read-chunk is a practical trade-off for throughput vs. cache efficiency and stack usage.
- Pre-allocating exact Content-Length is appropriate only if you must materialize in memory; otherwise, stream to disk/application.
- Chunked decoding must be defensive: validate sizes, enforce limits, and reject illegal header combinations.
- Stack vs. heap trade-offs hold: small, short-lived reads on stack; large or persistent data on heap; pools/arenas for advanced, measured performance needs.

----------------------------------------------------------------

## 10. Example Minimal Skeleton (HTTP/1.1 Header Read)

```c
int handle_connection(int fd, const struct limits *lim) {
    char hdr_stack[8192];
    char *hdr = hdr_stack; size_t used = 0, cap = sizeof(hdr_stack);
    bool heap = false;

    if (!set_nonblocking(fd)) return -1;

    while (used < lim->max_header_bytes) {
        int rc = poll_readable(fd, lim->header_timeout_ms);
        if (rc == 0) return http_408(fd);
        if (rc < 0) return -1;

        ssize_t n = recv(fd, hdr + used, cap - used, 0);
        if (n < 0 && errno == EAGAIN) continue;
        if (n <= 0) return -1;
        used += (size_t)n;

        if (has_double_crlf(hdr, used)) break;

        if (used == cap) {
            if (cap >= lim->max_header_bytes) return http_431(fd);
            size_t new_cap = cap * 2;
            if (new_cap > lim->max_header_bytes) new_cap = lim->max_header_bytes;
            char *new_hdr = heap ? realloc(hdr, new_cap) : malloc(new_cap);
            if (!new_hdr) return -1;
            if (!heap) memcpy(new_hdr, hdr_stack, used);
            hdr = new_hdr; cap = new_cap; heap = true;
        }
    }

    struct request req = {0};
    if (!parse_headers(hdr, used, &req)) return http_400(fd);

    // ... handle body per Content-Length / chunked ...

    if (heap) free(hdr);
    return 0;
}
```

----------------------------------------------------------------

By adhering to these guidelines, you’ll keep your C code maintainable and your HTTP server robust against malformed inputs, resource exhaustion, and performance pitfalls—all while documenting the crucial “why” and “contract” aspects that matter to future readers.

# Skiss på sheduler

```c
typedef struct
{
    HTTPServer httpServer;
    LinkedList *instances;
    smw_task *task;
} WeatherServer;

typedef struct
{
    HTTPServer_OnConnection onConnection;
    TCPServer tcpServer;
    smw_task *task;
} HTTPServer;

typedef struct
{
    int listen_fd;
    TCPServer_OnAccept onAccept;
    void *context;
    smw_task *task;
} TCPServer;

{
    LinkedList *instances;
    smw_task *task;
    protocol
    {
        on_event_cb HTTP_client_connected;
        smw_task *task_protocol;
        transport
        {
            int listen_fd;
            on_event_cb TCP_socket_connected;
            void *context;
            smw_task *task_transport;
        };
    };
}
WeatherServer;

// "ASCII klient", varje rad är bara tecken. Är hur modem funkar.

// SHEDULER från gemini
//-----------------------------------------------------------
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>
#include <math.h>

// --- Configuration ---
#define MAX_TASKS 16
#define RUN_CYCLES 5000 // Total number of times all tasks will be executed (simulation length)

// --- Data Structures ---

// Define the function signature for a scheduled task
typedef void (*TaskFunction)(int);

/**
 * @brief Represents a single scheduled task with metrics.
 */
typedef struct
{
    char name[64];
    TaskFunction func;
    int work_load_ms;  // Simulated work load/duration in milliseconds
    int runs;          // Number of times this task has run
    double total_time; // Total accumulated execution time in milliseconds
    double avg_time;   // Average execution time
} Task;

/**
 * @brief The Cooperative Scheduler structure.
 */
typedef struct
{
    Task tasks[MAX_TASKS];
    int task_count;
    int current_task_index; // Index of the next task to run
} Scheduler;

// --- Utility Functions ---

/**
 * @brief Calculates the time difference between two timeval structures in milliseconds.
 * @param end The ending time.
 * @param start The starting time.
 * @return The duration in milliseconds (double).
 */
double time_diff_ms(struct timeval *end, struct timeval *start)
{
    long seconds = end->tv_sec - start->tv_sec;
    long microseconds = end->tv_usec - start->tv_usec;

    // Convert to total milliseconds
    return (double)(seconds * 1000) + (double)microseconds / 1000.0;
}

/**
 * @brief Simulates synchronous, CPU-intensive work.
 * This is the code that would block the server thread if not managed.
 * @param duration_ms The target duration to busy-loop in milliseconds.
 */
void simulate_heavy_work(int duration_ms)
{
    if (duration_ms <= 0)
        return;

    struct timeval start, current;
    gettimeofday(&start, NULL);
    double elapsed_ms = 0;

    // Busy loop to consume CPU time
    volatile double dummy = 0;
    do
    {
        // Perform a simple non-optimized calculation to keep the CPU busy
        dummy += sin(rand() % 100);
        gettimeofday(&current, NULL);
        elapsed_ms = time_diff_ms(&current, &start);
    } while (elapsed_ms < duration_ms);
}

// --- Scheduler Implementation ---

/**
 * @brief Initializes the scheduler structure.
 * @param s A pointer to the Scheduler instance.
 */
void init_scheduler(Scheduler *s)
{
    s->task_count = 0;
    s->current_task_index = 0;
    // Clear all task slots
    memset(s->tasks, 0, sizeof(s->tasks));
}

/**
 * @brief Adds a task to the scheduler's queue.
 * @param s A pointer to the Scheduler instance.
 * @param name The task name.
 * @param func The function pointer for the task.
 * @param work_load_ms The simulated CPU work time for this task.
 */
void add_task(Scheduler *s, const char *name, TaskFunction func, int work_load_ms)
{
    if (s->task_count >= MAX_TASKS)
    {
        fprintf(stderr, "Error: Maximum number of tasks reached (%d).\n", MAX_TASKS);
        return;
    }

    Task *new_task = &s->tasks[s->task_count];
    strncpy(new_task->name, name, sizeof(new_task->name) - 1);
    new_task->func = func;
    new_task->work_load_ms = work_load_ms;
    new_task->runs = 0;
    new_task->total_time = 0.0;
    new_task->avg_time = 0.0;

    s->task_count++;
    printf("Registered Task %d: %s (Work Load: %dms)\n", s->task_count, name, work_load_ms);
}

/**
 * @brief Executes the next scheduled task, tracks its metrics, and yields.
 * This function simulates one cycle of the cooperative event loop.
 * @param s A pointer to the Scheduler instance.
 * @return 1 if a task was executed, 0 if the queue is empty.
 */
int execute_next_task(Scheduler *s)
{
    if (s->task_count == 0)
    {
        return 0;
    }

    // Get the task to run
    Task *task = &s->tasks[s->current_task_index];

    struct timeval start, end;
    gettimeofday(&start, NULL); // Record start time

    // --- Cooperative Execution ---
    // 1. Run the task function, passing its simulated work load.
    task->func(task->work_load_ms);
    // 2. The task returns control to the scheduler.

    gettimeofday(&end, NULL); // Record end time

    // --- Metric Calculation ---
    double duration_ms = time_diff_ms(&end, &start);

    task->runs++;
    task->total_time += duration_ms;
    task->avg_time = task->total_time / task->runs;

    // --- Yield/Advance ---
    // Move the index to the next task in the round-robin fashion.
    s->current_task_index = (s->current_task_index + 1) % s->task_count;

    // In a real server: The thread now checks for I/O events (select/epoll)
    // before the main loop calls execute_next_task again.

    return 1;
}

/**
 * @brief Prints the collected execution metrics for all tasks.
 * @param s A pointer to the Scheduler instance.
 */
void print_metrics(Scheduler *s)
{
    printf("\n--- Cooperative Scheduler Metrics (After %d cycles) ---\n", RUN_CYCLES);

    // Find the task with the highest average time (the "bottleneck")
    double max_avg_time = 0.0;
    for (int i = 0; i < s->task_count; i++)
    {
        if (s->tasks[i].avg_time > max_avg_time)
        {
            max_avg_time = s->tasks[i].avg_time;
        }
    }

    printf("%-25s | %-10s | %-12s | %-10s | %s\n",
           "TASK NAME", "RUNS", "AVG TIME (ms)", "TOTAL (s)", "STATUS");
    printf("------------------------------------------------------------------------\n");

    for (int i = 0; i < s->task_count; i++)
    {
        Task *task = &s->tasks[i];

        // Check if this task is the current bottleneck
        const char *status = "";
        if (task->avg_time == max_avg_time && task->runs > 0)
        {
            status = "* BOTTLENECK"; // Highlight the slowest task
        }

        printf("%-25s | %-10d | %-12.3f | %-10.2f | %s\n",
               task->name,
               task->runs,
               task->avg_time,
               task->total_time / 1000.0,
               status);
    }
    printf("------------------------------------------------------------------------\n");
    printf("Note: Cooperative scheduling relies on tasks yielding control quickly (small work_load_ms).\n");
}

// --- Demo Tasks ---

void task_data_processing(int duration)
{
    // This task simulates heavy calculation
    simulate_heavy_work(duration);
    // printf("Data Processing Done.\n"); // Uncomment for verbose logging
}

void task_log_cleanup(int duration)
{
    // This task simulates file I/O or cleanup
    simulate_heavy_work(duration);
    // printf("Log Cleanup Done.\n");
}

void task_user_session_update(int duration)
{
    // This task simulates database interaction
    simulate_heavy_work(duration);
    // printf("Session Update Done.\n");
}

// --- Main Program ---

int main()
{
    Scheduler scheduler;
    init_scheduler(&scheduler);

    // Seed for random work simulation
    srand(time(NULL));

    // Register tasks with varying simulated work loads.
    // Notice that "Session Update" is given a heavy work load (30ms),
    // which should make it the bottleneck.
    add_task(&scheduler, "Data Processing (10ms)", task_data_processing, 10);
    add_task(&scheduler, "Log Cleanup (5ms)", task_log_cleanup, 5);
    add_task(&scheduler, "Session Update (30ms)", task_user_session_update, 30);
    add_task(&scheduler, "Cache Refresh (2ms)", task_data_processing, 2);

    printf("\nStarting Cooperative Scheduler Simulation for %d cycles...\n", RUN_CYCLES);
    printf("----------------------------------------------------------\n");

    // --- The Cooperative Loop ---
    for (int cycle = 0; cycle < RUN_CYCLES; cycle++)
    {
        if (!execute_next_task(&scheduler))
        {
            break; // Should not happen with registered tasks
        }

        // In a real web server, this is where the thread would check for
        // incoming network events (via select/epoll) before the next scheduler call.
        // We simulate this "event loop breathing room" by sleeping briefly.
        // If we didn't sleep, this loop would block the terminal until finished.
        usleep(1000); // 1ms sleep to yield to OS and other processes
    }
    //

    print_metrics(&scheduler);

    return 0;
}
```
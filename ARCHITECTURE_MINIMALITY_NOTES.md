# UB-WeatherServer – Why the Architecture Is "Minimal and Educational"

Date: 2025-11-18
Branch: `feature/new-scheduler`

This document explains why the current architecture is best described as **minimal and educational**, and what would be required to move it toward a more **production‑grade** server architecture.

---

## 1. What "Minimal and Educational" Means Here

In this project, "minimal and educational" means:

- **Small, focused scope:**
  - The codebase demonstrates a few key ideas: cooperative scheduling, task/state machines, non‑blocking sockets, and basic server wiring.
  - It intentionally avoids many concerns that real servers must handle (robust error handling, concurrency scaling, observability, configuration complexity, etc.).

- **Straightforward control flow:**
  - The scheduler is a single loop that walks an array of tasks and calls their `run` function.
  - No threads, no `select`/`poll`/`epoll`, no complex event loops.

- **Explicit state machines instead of hidden magic:**
  - `w_client_run` shows the full lifecycle of a connection in clearly enumerated states (`READING → PARSING → PROCESSING → SENDING → DONE`).
  - Transitions are controlled directly in code; this is great for teaching how cooperative multitasking works.

- **Simple ownership and lifetime story (for the demo scale):**
  - Tasks and their contexts are manually allocated and freed by the scheduler.
  - The scheduler runs until `task_count == 0`, then exits.

- **Didactic over complete:**
  - The code includes comments and TODOs about where things _could_ be improved (e.g., pluggable listen strategies, better error handling) without implementing all of them yet.

---

## 2. Concrete Minimal Choices in This Codebase

Several specific design decisions keep the architecture lean and suitable for learning:

### 2.1 Scheduler Design

- **Single-threaded, cooperative model:**
  - All tasks run on the same thread in a simple `while (task_count > 0)` loop.
  - There is no concurrency complexity beyond making sure tasks don’t block.

- **Fixed-size task array (`MAX_TASKS = 5`):**
  - Tasks are stored in a fixed array; adding a task scans for the first empty slot.
  - This keeps memory management simple and predictable.

- **No integrated I/O multiplexing:**
  - The scheduler does not know about file descriptors, readiness, or timeouts.
  - Tasks themselves call `accept`, `send`, etc., and decide whether to exit/continue.

### 2.2 Server and Client Responsibility

- **`w_server` only does socket setup + accept:**
  - Binds, listens, and exposes a scheduler task that calls `accept` in non‑blocking mode.
  - Once a client is accepted, it delegates almost everything to `w_client`.

- **Client logic is intentionally basic:**
  - `w_client_run` uses artificial `sleep_ms` calls and a trivial response to simulate work.
  - The actual protocol handling (HTTP parsing, routing, weather logic) is either stubbed out or not present.

### 2.3 Limited Configuration and Lifecycle Management

- **Configuration surface is small:**
  - `w_server_config` only includes address, port, and backlog.
  - No system for timeouts, TLS, logging levels, feature toggles, or plugins.

- **Lifecycle is mostly "run until done, then exit":**
  - `main` sets up, calls `mj_scheduler_run`, then tears down the scheduler.
  - There is no notion of graceful shutdown, reload, or dynamic reconfiguration.

---

## 3. What Would Be Needed to Move Beyond Minimal/Educational

To make the architecture more akin to a **general-purpose, robust server**, you’d gradually add capabilities in several dimensions. Below are the main areas and concrete steps.

### 3.1 Scheduler and Concurrency Model

**Current:** Single-threaded, cooperative, fixed `MAX_TASKS` array, no I/O multiplexing.

**To evolve:**

- **Dynamic task management:**
  - Replace `MAX_TASKS` and the fixed array with a dynamic structure (e.g., dynamic array, intrusive list, or ring buffer).
  - Introduce configurable limits (`max_tasks`, `max_clients`) enforced by the scheduler.

- **I/O multiplexing integration:**
  - Introduce a basic event loop that uses `select`, `poll`, or `epoll` to wait for readiness on sockets.
  - Let the scheduler sleep until there is actual work to do, reducing CPU usage and scaling to many connections.

- **Timers and scheduled events:**
  - Add support for timer tasks (e.g., run after N ms, run periodically) integrated into the scheduler.
  - This allows timeouts, periodic cleanup, metrics collection, etc.

- **Future option – multithreading:**
  - (Optional, and significantly more complex) Introduce worker threads for CPU‑heavy tasks while keeping the event loop single‑threaded for I/O.

### 3.2 Server and Listen Strategy Abstraction

**Current:** `w_server` is tightly coupled to a specific non‑blocking TCP `accept` loop.

**To evolve:**

- **Pluggable listen strategies:**
  - Define an interface for "listen tasks" (e.g., function pointer table or `struct` with `init`, `run`, `cleanup`).
  - Move `w_server_listen_TCP_nonblocking` into its own module that implements this interface.
  - Allow switching between strategies at compile time or via configuration (blocking, non‑blocking, `select`-based, TLS‑wrapped, etc.).

- **Explicit `w_server_destroy`:**
  - Implement and use a `w_server_destroy(w_server*)` that closes sockets, removes any server‑owned tasks, and frees the `w_server` instance.
  - Ensure `main` owns the `w_server*` and always calls `w_server_destroy` on exit.

- **Connection limit and back-pressure:**
  - Track the number of active client tasks and stop accepting new connections when a limit is reached.
  - Resume accepting when load drops.

### 3.3 Client Handling and Protocol Layer

**Current:** Client state machine is mostly a stub/demo with artificial sleeps and a trivial response.

**To evolve:**

- **Real protocol parsing:**
  - Integrate an HTTP parser (you already have `HTTPParser` in `src/libs`) and parse real HTTP requests.
  - Separate parsing from business logic: one layer to handle HTTP, another to implement the "weather" API.

- **Robust state transitions:**
  - Handle partial reads/writes, large request bodies, and malformed input.
  - Introduce per‑client timeouts (e.g., if a client doesn’t send anything for N seconds, close the connection).

- **Reusable handler architecture:**
  - Define an interface for handlers (e.g., route registration, middleware) so that adding endpoints does not require editing the core client state machine.

### 3.4 Error Handling, Observability, and Diagnostics

**Current:** Errors are mostly printed to `stderr` or `stdout` with `printf`/`fprintf`, and `w_server_error` is used only at creation time.

**To evolve:**

- **Centralized logging abstraction:**
  - Create a small logging API (`log_debug`, `log_info`, `log_warn`, `log_error`) that can be swapped or extended.
  - Add contextual information (client ID, remote address, request path, etc.) to log messages.

- **Structured error handling:**
  - Ensure every failure path sets a meaningful error code and that callers react appropriately.
  - For example, `w_server_create` should always set `last_error` and `main` should print human‑friendly diagnostics based on it.

- **Metrics and health indicators:**
  - Track and expose metrics such as active clients, total connections, error counts, and average request latency.
  - Consider an internal admin endpoint or metrics dump to stdout for debugging.

### 3.5 Configuration, Deployment, and Lifecycle

**Current:** Only address and port are configurable via CLI; lifecycle is essentially "start, run until no tasks, exit".

**To evolve:**

- **Richer configuration:**
  - Extend `w_server_config` with fields for:
    - Maximum client connections
    - Read/write timeouts
    - Logging level/target
    - Feature flags (e.g., enable metrics endpoint)
  - Load configuration from environment variables or a config file in addition to CLI args.

- **Graceful shutdown:**
  - Add a mechanism to signal the scheduler to stop accepting new clients and let existing ones finish.
  - Integrate with signals (e.g., handle `SIGINT`/`SIGTERM`) to trigger a graceful stop instead of abrupt exit.

- **Restartability and reload:**
  - (Optional) Support reloading configuration without restarting the process (e.g., rebind to a new port, change logging level).

### 3.6 Testing, Safety, and Reliability

**Current:** Emphasis on demonstrating flow rather than guaranteeing reliability under all conditions.

**To evolve:**

- **Unit and integration tests:**
  - Tests for the scheduler (task add/remove, run order, error paths).
  - Tests for `w_client` state transitions and error conditions.
  - Integration tests using the existing `load_test_*` tools to validate behavior under load.

- **Defensive programming:**
  - More assertions or checks around assumptions (e.g., that task removal is not called outside a task, that buffers are not overrun).

- **Memory safety tooling:**
  - Run under tools like `valgrind` or `ASan` to catch leaks and invalid accesses.

---

## 4. Summary

The project is "minimal and educational" because it focuses on **clarity over completeness**: it deliberately shows core concepts (cooperative scheduling, state machines, non‑blocking accepts) without the many additional layers required for a full‑featured production server.

To move beyond that, you would incrementally introduce:

- A more capable **scheduler/event loop** (dynamic tasks, I/O multiplexing, timeouts).
- A **pluggable and fully managed server lifecycle** (listen strategies, cleanup, graceful shutdown).
- A robust **client/protocol stack** (HTTP parsing, proper state management, timeouts).
- Richer **configuration, logging, and observability**.
- Comprehensive **testing and safety measures**.

You can treat the current codebase as a solid foundation: the abstractions are already shaped in a way that makes most of these evolutions possible without complete rewrites, but with deliberate, staged refinements.
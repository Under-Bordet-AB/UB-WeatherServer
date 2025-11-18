# UB-WeatherServer – Architecture Review

Date: 2025-11-18
Branch: `feature/new-scheduler`

## 1. High-Level Architecture

- **Process model:** Single-process, single-threaded, cooperative scheduler (`mj_scheduler`) that multiplexes a bounded set of tasks (`MAX_TASKS = 5`).
- **Concurrency model:** Explicit, manual scheduling loop in `mj_scheduler_run`; each task is a state machine (`run` function + `ctx`), invoked in round‑robin fashion.
- **Server responsibility:** `w_server` owns the listening socket and exposes a scheduler task (`w_server_listen_task`) that accepts new connections and registers `w_client` tasks.
- **Client responsibility:** `w_client_run` implements a simple multi‑step state machine for handling one client connection (READING → PARSING → PROCESSING → SENDING → DONE).
- **Entry point:** `main.c` wires together configuration, `w_server_create`, and the scheduler lifecycle.

Overall, the architecture is intentionally minimal and educational: it demonstrates cooperative scheduling concepts over a small HTTP‑like server.

## 2. Separation of Concerns

### Strengths

- **Clear role boundaries:**
  - `main.c` handles CLI args, configuration, and lifecycle orchestration.
  - `majjen.{h,c}` encapsulate the generic scheduler and task abstraction.
  - `w_server.{h,c}` is responsible for listening socket setup and exposing a task for accepting clients.
  - `w_client.{h,c}` encapsulate per‑connection behavior and state.
- **Task abstraction:** `mj_task` (`create`, `run`, `cleanup`, `ctx`) is generic enough to support other task types beyond networking (timers, background jobs, etc.).
- **Explicit state machines:** The client handler uses an explicit `enum`‑based state machine, making control flow easier to trace and reason about.

### Observations / Risks

- **Server owns both accept logic and scheduler task:**
  - Comment in `w_server.c` notes that `w_server_listen_TCP_nonblocking` should be factored into a separate module ("listen strategy"). Currently it is hard‑wired.
- **`w_server` lifecycle is only partially managed:**
  - There is a `w_server_cleanup` function, but it is not integrated into `main` or any task cleanup path; the listening fd and server struct are effectively leaked until process exit.
- **Scheduler lifetime vs. server lifetime:**
  - `main` destroys the scheduler after `mj_scheduler_run` completes, but the destroy function explicitly fails if there are still tasks (`task_count > 0`). Since `mj_scheduler_run` only ends when `task_count == 0`, this is consistent, but it tightly couples exit behavior to task count.

## 3. Concurrency and Scheduling Model

### How it works today

- **Scheduler:**
  - `mj_scheduler_run` loops while `task_count > 0`.
  - For each iteration, it scans `task_list[0..MAX_TASKS-1]`, calling `run(scheduler, ctx)` on non‑NULL tasks.
  - Tasks must be non‑blocking; they yield control simply by returning.
- **Tasks:**
  - `w_server_listen_TCP_nonblocking`:
    - Non‑blocking `accept` on `listen_fd`.
    - On `EAGAIN/EWOULDBLOCK`, simply returns.
    - On success, sets the client socket non‑blocking, creates a `w_client` task, and adds it to the scheduler.
  - `w_client_run`:
    - Uses a state enum to progress through stages; all blocking behavior is explicitly simulated via `sleep_ms` to emulate load.
    - When done, calls `mj_scheduler_task_remove_current`, which in turn calls `w_client_cleanup`, frees the context, and removes the task from the scheduler.

### Strengths

- **Simple and predictable:** No threads, locks, or complex synchronization primitives.
- **Good for teaching:** Exposes the mechanics of cooperative scheduling and state machines clearly.
- **Explicit yielding:** All blocking or long operations are under the control of the task implementer (no hidden blocking in scheduler).

### Limitations

- **Bounded parallelism:**
  - `MAX_TASKS = 5` is a hard limit; beyond that, `mj_scheduler_task_add` fails with `ENOMEM`. This is fine for demos but not for a real server.
- **Busy‑loop potential:**
  - `mj_scheduler_run` has no integrated I/O multiplexing (`select`/`poll`/`epoll`). Tasks are called in tight loops and internally call `accept`, `send`, etc.
  - Under load or with no I/O ready, the loop can spin, wasting CPU.
- **No explicit back‑pressure:**
  - If the listening task is always active and `MAX_TASKS` is high, there is no built‑in mechanism to stop accepting new clients when the system is saturated.

## 4. Error Handling and Robustness

### Strengths

- **Enumerated server errors:** `w_server_error` gives a clear taxonomy of initialization failures.
- **Early validation:** `w_server_create` validates config (null checks, port string presence) and short‑circuits on failures with error messages.
- **Scheduler argument validation:** Functions like `mj_scheduler_run`, `mj_scheduler_task_add`, `mj_scheduler_task_remove_current`, and `mj_scheduler_destroy` validate input pointers and set `errno`.

### Gaps / Risks

- **Error propagation from `w_server_create`:**
  - `w_server_create` sets `last_error` but only communicates failure by returning `NULL`; `main` does not inspect `last_error` and instead prints generic messages.
- **`listen` failure path:**
  - On `listen` failure, the code `perror("listen"); free(server); return NULL;` does not set a specific `last_error` value.
- **Resource leaks on partial failure:**
  - If task allocation fails after the listening socket is created, the socket is not closed before returning `NULL`.
- **Cleanup for server and global resources:**
  - `main` does not call any cleanup for `w_server` or other resources; it only destroys the scheduler.
- **`w_client` cleanup:**
  - The cleanup routine closes the socket and frees buffers, but the scheduler cleanup blindly `free`s `task->ctx` after calling `cleanup`. If `cleanup` were to `free` the context internally (not currently the case), this would double‑free; the contract is implicit.

## 5. Configuration and Extensibility

### Current State

- **Configuration surface:**
  - `main` accepts two CLI args: `port` and `address`, then builds a `w_server_config` struct.
  - Only `address`, `port`, and `backlog` are configurable; TLS, timeouts, logging, and protocol settings are not.
- **Extensibility points:**
  - `mj_task` abstraction allows new task types to be added (timers, maintenance tasks, metrics reporters).
  - Server listen behavior is currently fixed but has a clear TODO to support different listening strategies.

### Opportunities

- **Pluggable listen strategies:**
  - Extract `w_server_listen_TCP_nonblocking` into separate modules (e.g., `w_server_listen_tcp_nb.{h,c}`) and let `w_server` hold a function pointer or strategy object.
- **Config struct evolution:**
  - Extend `w_server_config` with optional fields: connection limits, timeouts, logging callbacks, metric hooks.
- **Scheduler extensions:**
  - Introduce prioritized tasks, timers, or I/O wait integration (e.g., a wake‑up when any fd is readable or writable).

## 6. Code Quality and Maintainability

### Positive Aspects

- **Readable naming:** Most identifiers are descriptive (`w_server_listen_TCP_nonblocking`, `mj_scheduler_task_remove_current`).
- **Comments and TODOs:** The code includes meaningful comments about what functions do and where the architecture should evolve (e.g., separate listen strategies, standardized error handling).
- **Small, focused units:** Files are small and functions mostly do one thing.

### Areas for Improvement

- **Header location consistency:**
  - `majjen.h` resides in `src/libs`, but is included as `"majjen.h"` from several places; `main.c` currently includes `"majjen.h"` but not from `src/libs` path. Ensuring a consistent include strategy (via compiler include paths or `libs/majjen.h`) would make the structure clearer.
- **Explicit contracts for `cleanup`:**
  - Document the expectations in `majjen.h`: whether `cleanup` is allowed to free `ctx` or just internal buffers.
- **Logging and diagnostics:**
  - `fprintf(stderr, ...)` and `printf(...)` are used directly; a small logging wrapper could add timestamps, severity levels, and per‑component tags.

## 7. Suggested Next Steps

### Short-Term

- **1. Integrate `w_server` cleanup into `main`:**
  - Keep a pointer to the `w_server` instance in `main` and call a new `w_server_destroy(w_server*)` which:
    - Calls `w_server_cleanup` to close the listening socket.
    - Frees the `w_server` struct and associated task if still allocated.
- **2. Clarify scheduler/task ownership contracts:**
  - In `majjen.h`, document who owns `mj_task` and its `ctx`, and which components are responsible for freeing them.
- **3. Harden error reporting:**
  - Ensure all failure paths in `w_server_create` set `last_error` appropriately and that `main` prints user‑friendly messages based on it.

### Medium-Term

- **4. Make listen strategy pluggable:**
  - Introduce an abstraction around the accept loop so that different listening mechanisms (blocking, non‑blocking, `select`/`epoll`) can be plugged in without changing `w_server`.
- **5. Introduce basic I/O multiplexing:**
  - Add a simple `select`-based loop or a central I/O wait step in `mj_scheduler_run` to reduce busy‑looping and make the scheduler scale to more tasks.
- **6. Improve configuration surface:**
  - Extend `w_server_config` to include limits (max clients, timeouts) and connection behavior.

### Long-Term / Stretch Goals

- **7. Observability and metrics:**
  - Track per‑client and global metrics (active clients, requests/second, error rates) using `w_server->active_count` and expose them via logging or an admin endpoint.
- **8. Protocol separation:**
  - Introduce a thin HTTP parser/adapter layer so that `w_client` becomes protocol‑agnostic and the "weather" logic can be swapped out or reused.
- **9. Testing and simulation:**
  - Add unit tests for the scheduler and client state machine, plus load tests using the existing `load_test_*` binaries.

## 8. Summary

UB-WeatherServer has a clean, educational architecture centered around a simple cooperative scheduler and explicit state machines. The current design is well-suited as a teaching tool and a foundation for experimenting with scheduling strategies and server behavior. To evolve it into a more robust server, the main architectural investments would be better lifecycle management, pluggable listen strategies, basic I/O multiplexing, and a clearer ownership model for tasks and their contexts.

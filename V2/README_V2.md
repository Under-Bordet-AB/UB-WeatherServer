# V2 scaffolding — overview

This directory contains a small, opinionated scaffold for refactoring the project
into clear layers using snake_case names and a cooperative (yielding)
scheduler. The code is intentionally minimal: files contain TODOs and small
boilerplate so you can implement behavior incrementally.

Goals
- Clear separation of concerns: transport / protocol / app / libs
- Snake_case identifiers and descriptive names
- Cooperative scheduler for non-preemptive tasking
- Explicit state-machine skeletons for all session/connection logic

Layout (created)
- `src/transport_tcp/`
  - `tcp_listener.h/c`  — accept loop, spawn connections
  - `tcp_connection.h/c` — shared non-blocking read/write helpers
  - `tcp_client.h/c`    — outbound connect state-machine (optional)
- `src/protocol_http/`
  - `http_server.h/c`   — wire listener → connections
  - `http_connection.h/c` — per-connection state-machine, parsing glue
  - `http_parser.h/c`   — simple parser helpers (stubs)
- `src/app/`
  - `server.h/c`        — application server, session state-machine,
                          route handlers (example: `server_route_cities`)
- `src/libs/`
  - `scheduler.h/c`     — cooperative scheduler API (task registration,
                          yield/timeout helpers)

Design notes and conventions
- Names: snake_case, only lowercase. Callbacks end with `_cb`.
  Example: `protocol_http_connection_task_cb`, `server_session_request_cb`.
- Layer responsibilities:
  - transport_tcp: sockets, accept/connect, FD lifecycle.
  - protocol_http: HTTP framing, keep-alive, request detection; calls the app
    callback when a full request is ready.
  - app: routing, JSON formatting, session state-machine, business logic.
  - libs/scheduler: cooperative scheduler used by protocol and app tasks.
- State-machine pattern:
  - Each session/connection has an enum state and `step` function.
  - `session_result_t session_step(session_t *sess, uint64_t now_ms);`
  - `step` must be short and non-blocking. Use return value to indicate:
    - >= 0 : OK; value can be used to reschedule (0 = immediate)
    - <  0 : fatal error → destroy session
  - Skeleton: `switch (sess->state) { case state_reading: ... }`

Scheduler basics (what to implement)
- Minimal API (suggestion):
  - `typedef int (*task_cb_t)(void *ctx, uint64_t now_ms);` — return same semantics
  - `void *scheduler_create(void);`
  - `void scheduler_destroy(void *sched);`
  - `void *scheduler_add_task(void *sched, task_cb_t cb, void *ctx);`
  - `void scheduler_remove_task(void *sched, void *task);`
  - `void scheduler_run_once(void *sched, uint64_t now_ms);` or `scheduler_run_loop()`
- Tasks should be able to yield until a timestamp or until IO readiness.
- Integrate with `epoll`/`select` later for socket readiness to avoid busy-waiting.

How to proceed (recommended, small steps)
1. Implement `libs/scheduler.c` (task registration, simple tick loop).
2. Implement `transport_tcp/tcp_listener.c` to accept and create a `tcp_connection_t`.
3. Implement `protocol_http/http_connection.c` step: non-blocking read, detect full request,
   call `server_session_request_cb()` (app callback), then drive write.
4. Implement `src/app/server.c` session state-machine (use the header skeleton).
5. Hook things together in a small `main` that creates the scheduler, http server,
   and starts the run loop.
6. Replace TODOs incrementally and test with `curl` when the connection + parsing is ready.

Testing & notes
- V2 is a scaffold only — it doesn't provide a ready Makefile or runnable binary.
- Add a small `Makefile` or integrate sources into the main project's build.
- After implementing transport+protocol+app, smoke test:
  - Start server, run `curl -v http://localhost:8080/cities` (or your chosen port).
- Keep changes small and build after each module to catch regressions early.

Tips
- Keep `session_step` small; offload long operations (DB, upstream HTTP) to separate tasks.
- Use typed fields (avoid `void *user`) where possible to reduce casts.
- Use `wakeup_ms` in sessions to schedule time-based waits via the scheduler.

If you want, I can:
- Implement a working `libs/scheduler.c` reference (simple timer and task list).
- Add a tiny `V2/src/main.c` example that wires the pieces together.

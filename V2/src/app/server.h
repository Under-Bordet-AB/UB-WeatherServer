#ifndef V2_SRC_APP_SERVER_H
#define V2_SRC_APP_SERVER_H

#include <stddef.h>
#include <stdint.h>

/* Small constants for buffers (adjust as needed) */
#define SERVER_DEFAULT_REQ_CAP 4096
#define SERVER_DEFAULT_RESP_CAP 8192

/* Forward declarations for typed app-level objects you may add later */
typedef struct server_config_t server_config_t;

/* Session state machine states */
typedef enum {
  session_state_new,
  session_state_reading,
  session_state_parsing,
  session_state_handling,
  session_state_writing,
  session_state_closing,
  session_state_done,
  session_state_error
} session_state_t;

/* Return codes for session_step:
   - >=0 : request to (re)schedule (0 = immediate)
   - < 0 : fatal error, session should be destroyed */
typedef int session_result_t;

/* Top-level application/server
   - One instance per running program.
   - Owns global resources: config, caches, scheduler handle, API clients, etc.
*/
typedef struct server_t server_t;

/* Per-connection/session context
   - One instance per accepted client connection (or per logical session).
   - Store socket fd, session state, buffers, and a backref to server.
*/
typedef struct session_t session_t;

struct server_t {
  server_config_t *config; /* optional typed config (NULL = none) */
  void *scheduler;         /* opaque handle to your scheduler (optional) */
  void *user; /* app-specific global context (replace with concrete type) */
};

struct session_t {
  int client_fd;         /* socket file descriptor for this session */
  server_t *server;      /* backref to owning server (non-owning) */
  session_state_t state; /* state machine current state */

  /* simple request/response buffers (owned by session) */
  char *req_buf;
  size_t req_len;
  size_t req_cap;

  char *resp_buf;
  size_t resp_len;
  size_t resp_cap; /* <-- added to match server.c */
  size_t resp_sent;

  uint64_t last_activity_ms; /* monotonic time for timeouts */
  uint64_t wakeup_ms;        /* scheduler wake time when waiting */

  void *task_handle; /* opaque scheduler task handle (if used) */

  void *user; /* per-session application data (replace with concrete type) */
};

/* server lifecycle */
server_t *server_init(void);
void server_shutdown(server_t *s);

/* session lifecycle */
session_t *session_create(server_t *s, int client_fd);
void session_destroy(session_t *sess);

/* session state-machine step:
   - Called by cooperative scheduler or protocol task loop.
   - now_ms: current monotonic milliseconds (scheduler provides).
   - Return semantics:
     - >=0 : OK; value can be used to schedule next call (0 = asap).
     - < 0 : fatal error; caller should destroy session.
*/
session_result_t session_step(session_t *sess, uint64_t now_ms);

/* Route handlers
   - Implement application routes here.
   - Return 0 on success or negative on failure.
*/
int server_route_cities(server_t *s, session_t *sess);

/* Callback invoked by the protocol layer when a complete request is ready.
   - Called in the protocol/session task context (cooperative scheduler).
   - Must be non-blocking: parse request, schedule long work via
   scheduler/tasks.
   - Do not free session/server objects here; cleanup happens in session
   lifecycle.
   - Parameters:
     - s: global server instance (non-owning).
     - sess: per-session context (non-owning).
     - req: pointer to request bytes (not NUL-terminated).
     - len: request length.
*/
void server_session_request_cb(server_t *s, session_t *sess, const char *req,
                               size_t len);

#endif /* V2_SRC_APP_SERVER_H */
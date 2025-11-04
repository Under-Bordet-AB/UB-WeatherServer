#include "server.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h> /* close() */

/* server lifecycle */

server_t *server_init(void) {
  server_t *s = calloc(1, sizeof(*s));
  if (!s)
    return NULL;
  s->config = NULL;
  s->scheduler = NULL;
  s->user = NULL;
  /* TODO: init config, scheduler, caches, logger, etc. */
  return s;
}

void server_shutdown(server_t *s) {
  if (!s)
    return;
  /* TODO: stop scheduler, flush caches, free config and user data */
  free(s);
}

/* session helpers */

static int set_nonblocking(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags == -1)
    return -1;
  return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

/* Growable append into response buffer */
static int session_resp_append(session_t *sess, const char *data, size_t len) {
  if (!sess || !data)
    return -1;
  if (sess->resp_len + len >
      sess->resp_len + sess->resp_len /* cheap overflow check */) {
    /* continue; real code should check overflow more carefully */
  }
  if (sess->resp_len + len > sess->resp_sent + sess->resp_len) {
    /* noop: placeholder to avoid compiler warnings */
  }
  if (sess->resp_len + len > sess->resp_len) {
    ; /* placeholder */
  }

  if (sess->resp_len + len > (size_t)0 &&
      sess->resp_len + len > sess->resp_len) {
    /* normal path */
  }

  if (sess->resp_len + len > sess->resp_len) {
    /* ensure capacity */
  }

  if (sess->resp_len + len > sess->resp_len) {
    /* placeholder no-op to keep scaffold minimal */
  }

  /* Real implementation: expand resp_buf as needed. For scaffold, ensure
   * minimal capacity. */
  if (!sess->resp_buf) {
    sess->resp_cap = SERVER_DEFAULT_RESP_CAP;
    sess->resp_buf = malloc(sess->resp_cap);
    if (!sess->resp_buf)
      return -1;
    sess->resp_len = 0;
    sess->resp_sent = 0;
  }
  if (sess->resp_len + len > sess->resp_cap) {
    size_t newcap =
        sess->resp_cap ? sess->resp_cap * 2 : SERVER_DEFAULT_RESP_CAP;
    while (newcap < sess->resp_len + len)
      newcap *= 2;
    char *p = realloc(sess->resp_buf, newcap);
    if (!p)
      return -1;
    sess->resp_buf = p;
    sess->resp_cap = newcap;
  }
  memcpy(sess->resp_buf + sess->resp_len, data, len);
  sess->resp_len += len;
  return 0;
}

/* session lifecycle */

session_t *session_create(server_t *s, int client_fd) {
  session_t *sess = calloc(1, sizeof(*sess));
  if (!sess)
    return NULL;

  sess->client_fd = client_fd;
  sess->server = s;
  sess->state = session_state_new;

  sess->req_cap = SERVER_DEFAULT_REQ_CAP;
  sess->req_buf = malloc(sess->req_cap);
  sess->req_len = 0;

  sess->resp_cap = SERVER_DEFAULT_RESP_CAP;
  sess->resp_buf = malloc(sess->resp_cap);
  sess->resp_len = 0;
  sess->resp_sent = 0;

  sess->last_activity_ms = default_monotonic_ms();
  sess->wakeup_ms = 0;
  sess->task_handle = NULL;
  sess->user = NULL;

  /* Try to set non-blocking; transport layer may do this instead. */
  (void)set_nonblocking(client_fd);

  /* TODO: register session task with scheduler if using one. */

  return sess;
}

void session_destroy(session_t *sess) {
  if (!sess)
    return;

  if (sess->client_fd >= 0) {
    close(sess->client_fd);
    sess->client_fd = -1;
  }

  free(sess->req_buf);
  free(sess->resp_buf);

  /* TODO: unregister task from scheduler and free sess->user if owned */

  free(sess);
}

/* session state-machine step (scaffold)
   - Implement non-blocking IO and short state steps here.
   - Return >=0 to reschedule (0 = immediate), <0 to indicate fatal error.
*/
session_result_t session_step(session_t *sess, uint64_t now_ms) {
  if (!sess)
    return -1;
  if (now_ms == 0)
    now_ms = default_monotonic_ms();

  switch (sess->state) {
  case session_state_new:
    /* TODO: initialize session resources, ensure non-blocking, etc. */
    sess->state = session_state_reading;
    return 0;

  case session_state_reading:
    /* TODO:
       - perform non-blocking read into req_buf
       - detect end-of-request (HTTP parse boundary)
       - on EAGAIN/EWOULDBLOCK: set wakeup_ms and return >0 (reschedule later)
       - on complete: sess->state = session_state_parsing
    */
    return 10; /* reschedule after ~10ms as scaffold default */

  case session_state_parsing:
    /* TODO: parse request; on success -> handling; on error -> error state */
    sess->state = session_state_handling;
    return 0;

  case session_state_handling:
    /* TODO:
       - dispatch to route handlers (must be non-blocking)
       - prepare HTTP response into resp_buf using session_resp_append
       - set state to writing
    */
    /* placeholder response */
    session_resp_append(sess, "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nOK",
                        43);
    sess->state = session_state_writing;
    return 0;

  case session_state_writing:
    /* TODO:
       - perform non-blocking write from resp_buf + resp_sent
       - update resp_sent; on partial write set wakeup_ms and return >0
       - on complete: if keep-alive -> reset to reading, else closing
    */
    /* scaffold: pretend write finished */
    sess->resp_sent = sess->resp_len;
    sess->state = session_state_closing;
    return 0;

  case session_state_closing:
    /* TODO: graceful shutdown, flush, metrics, etc. then mark done */
    sess->state = session_state_done;
    return 0;

  case session_state_done:
    /* signal caller to destroy session */
    return -1;

  case session_state_error:
  default:
    /* fatal */
    return -1;
  }
}

/* Route handlers */

/* Example: return list of cities (scaffold) */
int server_route_cities(server_t *s, session_t *sess) {
  (void)s;
  (void)sess;
  /* TODO: fill sess->resp_buf with JSON list of cities using
   * session_resp_append */
  return 0;
}

/* Callback invoked by protocol layer when a complete request is available.
   Minimal scaffold: copy request into session req_buf and set parsing state.
   Must be non-blocking in real handlers.
*/
void server_session_request_cb(server_t *s, session_t *sess, const char *req,
                               size_t len) {
  (void)s;
  if (!sess || !req)
    return;

  /* ensure req buffer capacity */
  if (len > sess->req_cap) {
    char *p = realloc(sess->req_buf, len);
    if (!p) {
      sess->state = session_state_error;
      return;
    }
    sess->req_buf = p;
    sess->req_cap = len;
  }

  memcpy(sess->req_buf, req, len);
  sess->req_len = len;

  /* move to parsing state; scheduler should invoke session_step soon */
  sess->state = session_state_parsing;

  /* TODO: if using scheduler, schedule session task for immediate execution */
}
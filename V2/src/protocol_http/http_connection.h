/* http_connection.h - scaffold */
#ifndef v2_src_protocol_http_http_connection_h
#define v2_src_protocol_http_http_connection_h

#include <stddef.h>
#include <stdint.h>

typedef struct protocol_http_connection_t protocol_http_connection_t;

typedef enum {
  protocol_http_connection_state_init = 0,
  protocol_http_connection_state_reading,
  protocol_http_connection_state_parsing,
  protocol_http_connection_state_handling,
  protocol_http_connection_state_writing,
  protocol_http_connection_state_closed,
} protocol_http_connection_state_t;

struct protocol_http_connection_t {
  int client_fd;
  protocol_http_connection_state_t state;
  void *user;
};

/* Task callback for connection; intended to be registered with scheduler. */
typedef void (*protocol_http_connection_task_cb)(protocol_http_connection_t *c,
                                                 void *ctx);

/* Handle a parsed request for a connection */
void protocol_http_connection_handle_request(protocol_http_connection_t *c,
                                             const char *request_data,
                                             size_t len);

#endif /* v2_src_protocol_http_http_connection_h */

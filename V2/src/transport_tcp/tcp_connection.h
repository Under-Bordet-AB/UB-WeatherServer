/* tcp_connection.h - scaffold */
#ifndef v2_src_transport_tcp_tcp_connection_h
#define v2_src_transport_tcp_tcp_connection_h

#include <stddef.h>
#include <stdint.h>

typedef struct tcp_connection_t tcp_connection_t;

typedef enum {
  tcp_connection_state_init = 0,
  tcp_connection_state_reading,
  tcp_connection_state_writing,
  tcp_connection_state_closing,
  tcp_connection_state_closed,
} tcp_connection_state_t;

struct tcp_connection_t {
  int fd;
  tcp_connection_state_t state;
  void *user;
};

/* Basic operations */
ssize_t tcp_connection_read(tcp_connection_t *c, void *buf, size_t len);
ssize_t tcp_connection_write(tcp_connection_t *c, const void *buf, size_t len);
void tcp_connection_close(tcp_connection_t *c);

#endif /* v2_src_transport_tcp_tcp_connection_h */

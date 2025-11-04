/* tcp_connection.c - scaffold implementation */

#include "tcp_connection.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

ssize_t tcp_connection_read(tcp_connection_t *c, void *buf, size_t len) {
  if (!c || c->fd < 0)
    return -1;
  /* TODO: handle EINTR/EAGAIN, non-blocking I/O, partial reads. */
  (void)buf;
  (void)len;
  return -1; /* scaffold */
}

ssize_t tcp_connection_write(tcp_connection_t *c, const void *buf, size_t len) {
  if (!c || c->fd < 0)
    return -1;
  (void)buf;
  (void)len;
  /* TODO: implement write with retry for partial writes */
  return -1;
}

void tcp_connection_close(tcp_connection_t *c) {
  if (!c)
    return;
  /* TODO: shutdown/close socket and set state */
  c->state = tcp_connection_state_closed;
}

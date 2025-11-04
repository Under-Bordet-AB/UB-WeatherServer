/* tcp_client.c - scaffold implementation */

#include "tcp_client.h"
#include <stdio.h>
#include <stdlib.h>

tcp_client_t *tcp_client_connect(const char *host, uint16_t port) {
  (void)host;
  (void)port;
  /* TODO: resolve host, create socket, connect. Return allocated obj. */
  return NULL;
}

ssize_t tcp_client_send(tcp_client_t *c, const void *buf, size_t len) {
  if (!c)
    return -1;
  (void)buf;
  (void)len;
  /* TODO: implement send with error handling */
  return -1;
}

void tcp_client_close(tcp_client_t *c) {
  if (!c)
    return;
  /* TODO: close socket, free */
  free(c);
}

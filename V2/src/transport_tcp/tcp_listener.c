/* tcp_listener.c - scaffold implementation */

#include "tcp_listener.h"
#include <stdio.h>
#include <stdlib.h>

tcp_listener_t *tcp_listener_init(uint16_t port) {
  (void)port;
  /* TODO: create socket, bind, listen. Return allocated tcp_listener_t. */
  return NULL;
}

void tcp_listener_shutdown(tcp_listener_t *l) {
  if (!l)
    return;
  /* TODO: close socket, free resources */
  free(l);
}

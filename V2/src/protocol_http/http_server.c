/* http_server.c - scaffold */

#include "http_server.h"
#include <stdio.h>
#include <stdlib.h>

protocol_http_server_t *protocol_http_server_init(uint16_t port) {
  (void)port;
  /* TODO: create tcp_listener and wire accept callback to
   * protocol_http_server_accept_cb */
  return NULL;
}

void protocol_http_server_shutdown(protocol_http_server_t *s) {
  if (!s)
    return;
  /* TODO: cleanup listen socket and resources */
  free(s);
}

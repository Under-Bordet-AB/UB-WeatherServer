/* http_connection.c - scaffold */

#include "http_connection.h"
#include <stdio.h>
#include <stdlib.h>

void protocol_http_connection_handle_request(protocol_http_connection_t *c,
                                             const char *request_data,
                                             size_t len) {
  if (!c)
    return;
  (void)request_data;
  (void)len;
  /* TODO: parse request_data, route to app, prepare response, write back */
}

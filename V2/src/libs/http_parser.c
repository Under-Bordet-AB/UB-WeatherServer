/* protocol_http/http_parser.c - scaffold */

#include "http_parser.h"
#include <stdio.h>
#include <stdlib.h>

struct protocol_http_request_t {
  /* TODO: fill with method, path, headers, body pointers */
  void *user;
};

struct protocol_http_response_t {
  /* TODO: status, headers, body */
  void *user;
};

protocol_http_request_t *protocol_http_parser_parse(const char *data,
                                                    size_t len) {
  (void)data;
  (void)len;
  /* TODO: implement a real HTTP parser or delegate to libs/http_parser */
  return NULL;
}

void protocol_http_response_send(int client_fd,
                                 protocol_http_response_t *resp) {
  (void)client_fd;
  (void)resp;
  /* TODO: serialize response and write to client_fd */
}

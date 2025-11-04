/* http_server.h - scaffold */
#ifndef v2_src_protocol_http_http_server_h
#define v2_src_protocol_http_http_server_h

#include <stddef.h>
#include <stdint.h>

typedef struct protocol_http_server_t protocol_http_server_t;

typedef void (*protocol_http_server_accept_cb)(protocol_http_server_t *s,
                                               int client_fd, void *ctx);

struct protocol_http_server_t {
  int listen_fd;
  void *user;
};

protocol_http_server_t *protocol_http_server_init(uint16_t port);
void protocol_http_server_shutdown(protocol_http_server_t *s);

#endif /* v2_src_protocol_http_http_server_h */

/* tcp_client.h - scaffold */
#ifndef v2_src_transport_tcp_tcp_client_h
#define v2_src_transport_tcp_tcp_client_h

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

typedef struct tcp_client_t tcp_client_t;

struct tcp_client_t {
  int fd;
  void *user;
};

/* Connect to host:port. Returns allocated tcp_client_t or NULL. */
tcp_client_t *tcp_client_connect(const char *host, uint16_t port);

/* Send data through connected client. Returns number of bytes written or -1.
 * TODO: handle reconnect/backoff.
 */
ssize_t tcp_client_send(tcp_client_t *c, const void *buf, size_t len);

/* Close and free client */
void tcp_client_close(tcp_client_t *c);

#endif /* v2_src_transport_tcp_tcp_client_h */

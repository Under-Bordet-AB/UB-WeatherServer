/* tcp_listener.h - scaffold */
#ifndef v2_src_transport_tcp_tcp_listener_h
#define v2_src_transport_tcp_tcp_listener_h

#include <stddef.h>
#include <stdint.h>

typedef struct tcp_listener_t tcp_listener_t;

/* Opaque listener object */
struct tcp_listener_t {
  int fd; /* platform socket */
  void *user;
};

/* Initialize listener on given port. Returns allocated object or NULL.
 * TODO: implement socket creation, bind, listen.
 */
tcp_listener_t *tcp_listener_init(uint16_t port);

/* Callback invoked when accept yields a new connection */
typedef void (*tcp_listener_accept_cb)(tcp_listener_t *l, int client_fd,
                                       void *ctx);

/* Shutdown listener and free resources. */
void tcp_listener_shutdown(tcp_listener_t *l);

#endif /* v2_src_transport_tcp_tcp_listener_h */

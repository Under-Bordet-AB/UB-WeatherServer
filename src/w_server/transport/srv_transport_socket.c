#include "srv_transport_socket.h"
#include "srv_stream.h"
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

// Operations
static ssize_t socket_read(SrvStream* s, void* buf, size_t count) {
    if (!s || s->fd < 0)
        return -1;
    return read(s->fd, buf, count);
}

static ssize_t socket_write(SrvStream* s, const void* buf, size_t count) {
    if (!s || s->fd < 0)
        return -1;
    return write(s->fd, buf, count);
}

static void socket_close(SrvStream* s) {
    if (!s || s->fd < 0)
        return;
    close(s->fd);
    s->fd = -1;
}

// Static ops table.
// Since all transports of this type take the same functions we just
// return the address of this static variable.
static const SrvOpsTransport ops = {.read = socket_read,
                                    .write = socket_write,
                                    .close = socket_close,
                                    .ctx_create = NULL,
                                    .ctx_destroy = NULL};

// Getter
const SrvOpsTransport* srv_transport_get_socket() {
    return &ops;
}

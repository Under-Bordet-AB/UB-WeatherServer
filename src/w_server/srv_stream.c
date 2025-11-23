/*
    SrvStream is a generic interface all server modules must implement. This way the server can support any type of
    connection.

        - SrvStream: The main object for the flow of data
        - SrvOpsTransport: The functions provided by the module for input/output mechanisms (File, Socket, SSL)
        - SrvOpsProtocol: The logic handler for the data format (HTTP, JSON, etc.)

    USAGE:

*/

#include "srv_stream.h"
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

////////////////////////////////////////////////////////////////////////
//// FUNCTIONS

/*
    Life cycle
*/

SrvStream* SrvStream_create(void) {
    SrvStream* stream = calloc(1, sizeof(*stream));
    if (stream == NULL) {
        return NULL;
    }
    stream->fd = -1; // since "0" is stdin

    // Run transport and
    return stream;
}

int SrvStream_destroy(SrvStream* s) {
    if (s == NULL) {
        return -1;
    }

    // Close transport if set and close callback exists
    if (s->transport && s->fd >= 0 && s->transport->close) {
        s->transport->close(s);
    }

    // Free buffers if allocated
    if (s->rx_buffer != NULL) {
        free(s->rx_buffer);
        s->rx_buffer = NULL;
    }
    if (s->tx_buffer != NULL) {
        free(s->tx_buffer);
        s->tx_buffer = NULL;
    }

    // Free the stream
    free(s);

    return 0;
}
/*
    Transport / Protocol setters
*/

void SrvStream_transport_set(SrvStream* s, const SrvOpsTransport* ops, void* transport_ctx);
// void SrvStream_protocol_set(SrvStream* s, const SrvOpsProtocol* ops, void* protocol_ctx);

///*
//    Read buffer helpers (rx_ group)
//*/
//
// int SrvStream_rx_ensure_capacity(SrvStream* s, size_t min_free);
//
// void SrvStream_rx_compact(SrvStream* s);
//
// void SrvStream_rx_consume(SrvStream* s, size_t n);
//
// uint8_t* SrvStream_rx_peek(SrvStream* s);
//
// size_t SrvStream_rx_size(SrvStream* s);
//
///*
//    Write buffer helpers (tx_ group)
//*/
//
// int SrvStream_tx_append(SrvStream* s, const void* data, size_t len); // append data to tx_buffer
//
///*
//    Transport I/O operations (transport_ group)
//*/
//
// ssize_t SrvStream_transport_read(SrvStream* s);
//
// ssize_t SrvStream_transport_write_pending(SrvStream* s);
//
///*
//    Event handling (event_ group)
//*/
//
// int SrvStream_event_handle(SrvStream* s, unsigned int event_flags);
///*
//    Convenience accessors (fd_ group)
//*/

int SrvStream_set_fd_non_blocking(SrvStream* s) {
    if (!s || s->fd < 0) {
        return -1;
    }
    int flags = fcntl(s->fd, F_GETFL, 0);
    if (flags < 0) {
        return -1;
    }
    if (fcntl(s->fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        return -1;
    }
    return 0;
}

int SrvStream_set_fd_blocking(SrvStream* s) {
    if (!s || s->fd < 0) {
        return -1;
    }
    int flags = fcntl(s->fd, F_GETFL, 0);
    if (flags < 0) {
        return -1;
    }
    if (fcntl(s->fd, F_SETFL, flags & ~O_NONBLOCK) < 0) {
        return -1;
    }
    return 0;
}

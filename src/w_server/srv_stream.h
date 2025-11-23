/*
    DESCRIPTION:
        SrvStream is a generic interface all server modules must implement. This way the server can support any type of
        connection.

        - SrvStream: The main object for the flow of data
        - SrvOpsTransport: The functions provided by the module for input/output mechanisms (File, Socket, SSL)
        - SrvOpsProtocol: The logic handler for the data format (HTTP, JSON, etc.)

    MEMORY:
        Destroy frees:
            - tx & rx buffers
            - transport & protocol ctx

        Destroy DOES NOT free:
            - transport & protcol since they are static and shared by all
              of that type

    WHY:
    With this system we can now implement many diffrent protocols, for example:

        - Plain TCP socket
        - Mocks for testing
        - TLS/SSL (server and client)
        - UNIX domain socket
        - UDP / QUIC / datagram transports
        - WebSocket (upgrade + framed I/O)
        - HTTP CONNECT / proxy transport
        - Multiplexed transport instance (HTTP/2, HTTP/3/QUIC)
        - Serial / TTY transport
        - File-backed transport (serve files, logs)
        - In-memory / mock transport for tests
        - Connection pool / proxying transport
        - Specialized transports (Bluetooth, named pipes, platform-specific)
*/

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

////////////////////////////////////////////////////////////////////////
//// TYPES

typedef struct SrvStream SrvStream;

// TRANSPORT
typedef struct SrvOpsTransport {
    ssize_t (*read)(SrvStream* stream, void* buf, size_t count);        // Returns bytes read, 0 for EOF, -1 for error
    ssize_t (*write)(SrvStream* stream, const void* buf, size_t count); // Returns bytes written, -1 for error
    void (*close)(SrvStream* stream); // Clean up resources (close fd, free SSL context, etc.)

    // Optional ctx lifecycle
    void* (*ctx_create)(SrvStream* stream);
    void (*ctx_destroy)(SrvStream* stream, void* transport_ctx);

} SrvOpsTransport;

// PROTOCOL
typedef struct SrvOpsProtocol {
    const char* name;
    // Called when new data arrives in rx_buffer
    int (*on_data)(SrvStream* stream);
    // Called when the stream is ready to accept more data to write
    int (*on_writable)(SrvStream* stream);
} SrvOpsProtocol;

// STREAM
struct SrvStream {
    int fd; // OS handle (socket fd, file fd)

    const SrvOpsTransport* transport; // read write close functions for the fd
    void* transport_ctx; // Optional context that might be needed for the transport (e.g., SSL* object, file offset)

    const SrvOpsProtocol* protocol; // "on_data" and "on_writable" functions. (HTTP/1.1, WebSocket, FTP, or custom etc).
    void* protocol_ctx;             // Main data and state for the specific SrvStream instance

    // Buffers
    uint8_t* rx_buffer; // read, send, etc buffer
    size_t rx_len;
    size_t rx_pos;

    uint8_t* tx_buffer; // write, recv etc buffer
    size_t tx_len;
    size_t tx_pos;
};

////////////////////////////////////////////////////////////////////////
//// FUNCTIONS

/*
    Life cycle
*/

SrvStream* SrvStream_create(void);
int SrvStream_destroy(SrvStream* s);

/*
    Transport / Protocol
*/

void SrvStream_transport_set(SrvStream* s, const SrvOpsTransport* ops, void* transport_ctx);
// void SrvStream_protocol_set(SrvStream* s, const SrvOpsProtocol* ops, void* protocol_ctx);

/*
    Read buffer helpers (rx_ group)
*/

//// rx_buffer has at least min_free space at end
// int SrvStream_rx_ensure_capacity(SrvStream* s, size_t min_free);
//
//// memmove unconsumed bytes to buffer beginning
// void SrvStream_rx_compact(SrvStream* s);
//
//// advance rx_pos by n; resets when empty
// void SrvStream_rx_consume(SrvStream* s, size_t n);
//
//// pointer to first unread byte (rx_buffer + rx_pos)
// uint8_t* SrvStream_rx_peek(SrvStream* s);
//
//// number of unread bytes
// size_t SrvStream_rx_size(SrvStream* s);
//
///*
//    Write buffer helpers (tx_ group)
//*/
//
//// append data to tx_buffer
// int SrvStream_tx_append(SrvStream* s, const void* data, size_t len);
//
///*
//    Transport I/O operations (transport_ group)
//*/
//
//// reads into rx_buffer using transport->read
// ssize_t SrvStream_transport_read(SrvStream* s);
//
//// writes tx_buffer using transport->write
// ssize_t SrvStream_transport_write_pending(SrvStream* s);
//
//
//// perform reads/writes and call protocol callbacks
// int SrvStream_event_handle(SrvStream* s, unsigned int event_flags);

/*
    Convenience accessors (fd_ group)
*/
int SrvStream_set_fd_blocking(SrvStream* s);
int SrvStream_set_fd_non_blocking(SrvStream* s);
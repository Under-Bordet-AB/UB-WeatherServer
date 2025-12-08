# connecitons som försöker ansluta på HTTPS (ochså annan port?)
DÅ kopplar vi in TLS modulen istället för gamla http modulen.


## Important
The only thing that changes is the transport layer.
You still write normal HTTP strings, but instead of send() and recv(), you use:

mbedtls_ssl_write()

mbedtls_ssl_read()

### With TLS inserted
TCP Socket
   ↓
 TLS Protocol  ← handled by mbedTLS
   ↓
 HTTP Protocol
   ↓
 Application

### Sending data
HTTP data
 ↓
mbedtls_ssl_write()  ← encrypts data
 ↓
TLS Record Layer
 ↓
TCP Socket (mbedtls_net_send)

### Receiving data
TCP Socket (mbedtls_net_recv)
 ↓
TLS Record Layer
 ↓
mbedtls_ssl_read()  ← decrypts data
 ↓
HTTP data


## termer
#### BIO = “Basic Input/Output interface”
t is not a socket and not a buffer.
It is a set of callbacks the TLS engine uses to talk to your transport layer.

In mbedTLS, the TLS engine (mbedtls_ssl_context) does not know anything about sockets.
It needs functions for:
how to send bytes
how to receive bytes
a pointer to your socket or custom context

BIO setup:
mbedtls_ssl_set_bio(
    &ssl,
    &net,              // our socket wrapper
    mbedtls_net_send,  // how TLS writes to TCP
    mbedtls_net_recv,  // how TLS reads from TCP
    NULL
);
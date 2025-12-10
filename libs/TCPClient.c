#include "TCPClient.h"

int TCPClient_Initiate(TCPClient* c, int fd) {
    c->fd = fd;
    c->ssl = NULL;
    c->tls_handshake_done = 0;
    return 0;
}

int TCPClient_Connect(TCPClient* c, const char* host, const char* port) {
    if (c->fd >= 0) return -1;

    struct addrinfo hints = {0};
    struct addrinfo* res = NULL;

    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    if (getaddrinfo(host, port, &hints, &res) != 0) return -1;

    /*
    Funktionen getaddrinfo() kan ge en länkad lista av adressförslag för samma värd och port.
    Till exempel kan en server ha både IPv4- och IPv6-adresser, eller flera nätverkskort.

    Varje nod i listan (struct addrinfo) innehåller en möjlig adress att prova.
    Om första adressen inte fungerar (t.ex. connect() misslyckas), försöker man nästa.
    */

    int fd = -1;
    for (struct addrinfo* rp = res; rp; rp = rp->ai_next) {
        fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);

        if (fd < 0) continue;

        if (connect(fd, rp->ai_addr, rp->ai_addrlen) == 0) break;

        close(fd);
        fd = -1;
    }

    freeaddrinfo(res);
    if (fd < 0) return -1;

    c->fd = fd;
    return 0;
}

int TCPClient_SetSSL(TCPClient* c, mbedtls_ssl_context* ssl) {
    if (c == NULL || ssl == NULL) return -1;
    c->ssl = ssl;
    c->tls_handshake_done = 0;
    return 0;
}

int TCPClient_TLS_Handshake(TCPClient* c) {
    if (c->ssl == NULL) return -1; // No SSL context

    if (c->tls_handshake_done) return 0; // Already done

    int ret = mbedtls_ssl_handshake(c->ssl);

    if (ret == 0) {
        // Handshake successful
        c->tls_handshake_done = 1;
        return 0;
    } else if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
        // Need to retry (non-blocking)
        return ret;
    } else {
        // Error
        return ret;
    }
}

int TCPClient_Write(TCPClient* c, const uint8_t* buf, int len) {
    if (c->ssl != NULL && c->tls_handshake_done) {
        // Use TLS write
        return mbedtls_ssl_write(c->ssl, buf, len);
    }
    // Use plain TCP write
    return send(c->fd, buf, len, MSG_NOSIGNAL);
}

int TCPClient_Read(TCPClient* c, uint8_t* buf, int len) {
    if (c->ssl != NULL && c->tls_handshake_done) {
        // Use TLS read (non-blocking via BIO)
        return mbedtls_ssl_read(c->ssl, buf, len);
    }
    // Use plain TCP read (non-blocking)
    return recv(c->fd, buf, len, MSG_DONTWAIT); // icke-blockerande läsning
}

void TCPClient_Disconnect(TCPClient* c) {
    if (c->fd >= 0) close(c->fd);

    c->fd = -1;
    c->ssl = NULL;
    c->tls_handshake_done = 0;
}

void TCPClient_Dispose(TCPClient* c) {
    TCPClient_Disconnect(c);
}
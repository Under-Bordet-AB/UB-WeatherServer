#include "TSLServer.h"
#include <stdlib.h>

#include "mbedtls/ctr_drbg.h"
#include "mbedtls/debug.h"
#include "mbedtls/entropy.h"
#include "mbedtls/net_sockets.h"
#include "mbedtls/ssl.h"

//-----------------Internal Functions-----------------

void TSLServer_TaskWork(void* _Context, uint64_t _MonTime);

//----------------------------------------------------

int TSLServer_Initiate(TSLServer* _Server, const char* _Port, TSLServer_OnAccept _OnAccept, void* _Context) {
    _Server->recent_connections = 0;
    _Server->recent_connections_time = 0;

    _Server->onAccept = _OnAccept;
    _Server->context = _Context;

    // Starta upp TSL listening socket

    struct addrinfo hints = {0}, *res = NULL;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    if (getaddrinfo(NULL, _Port, &hints, &res) != 0) return -1;

    int fd = -1;
    for (struct addrinfo* rp = res; rp; rp = rp->ai_next) {
        fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd < 0) continue;

        int yes = 1;
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
        if (bind(fd, rp->ai_addr, rp->ai_addrlen) == 0) break;

        close(fd);
        fd = -1;
    }

    freeaddrinfo(res);
    if (fd < 0) return -1;

    if (listen(fd, TSLServer_MAX_CLIENTS) < 0) {
        close(fd);
        return -1;
    }

    TSLServer_Nonblocking(fd);

    _Server->listen_fd = fd;

    // Dra igång mbedTLS
    mbedtls_entropy_init(&_Server->entropy);
    mbedtls_ctr_drbg_init(&_Server->ctr_drbg);
    mbedtls_ssl_config_init(&_Server->conf);
    mbedtls_x509_crt_init(&_Server->cert);
    mbedtls_pk_init(&_Server->pkey);

    const char* pers = "tls_server";
    int ret = mbedtls_ctr_drbg_seed(&_Server->ctr_drbg, mbedtls_entropy_func, &_Server->entropy, (const unsigned char*)pers, strlen(pers));
    if (ret != 0) {
        printf("mbedtls_ctr_drbg_seed failed: %d\n", ret);
        return -1;
    }

    // ladda certifikat och nycklar
    ret = mbedtls_x509_crt_parse_file(&_Server->cert, "server.crt");
    if (ret != 0) {
        printf("crt_parse_file failed: -0x%x\n", -ret);
        return -1;
    }

    ret = mbedtls_pk_parse_keyfile(&_Server->pkey, "server.key", NULL);
    if (ret != 0) {
        printf("pk_parse_keyfile failed: -0x%x\n", -ret);
        return -1;
    }

    // SSL config
    ret = mbedtls_ssl_config_defaults(&_Server->conf, MBEDTLS_SSL_IS_SERVER, MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT);
    if (ret != 0) {
        printf("ssl_config_defaults failed: %d\n", ret);
        return -1;
    }

    mbedtls_ssl_conf_rng(&_Server->conf, mbedtls_ctr_drbg_random, &_Server->ctr_drbg);

    mbedtls_ssl_conf_ca_chain(&_Server->conf, _Server->cert.next, NULL);

    ret = mbedtls_ssl_conf_own_cert(&_Server->conf, &_Server->cert, &_Server->pkey);
    if (ret != 0) {
        printf("ssl_conf_own_cert failed: %d\n", ret);
        return -1;
    }

    _Server->task = smw_createTask(_Server, TSLServer_TaskWork);

    return 0;
}

int TSLServer_InitiatePtr(const char* _Port, TSLServer_OnAccept _OnAccept, void* _Context, TSLServer** _ServerPtr) {
    if (_ServerPtr == NULL) return -1;

    TSLServer* _Server = (TSLServer*)malloc(sizeof(TSLServer));
    if (_Server == NULL) return -2;

    int result = TSLServer_Initiate(_Server, _Port, _OnAccept, _Context);
    if (result != 0) {
        free(_Server);
        return result;
    }

    *(_ServerPtr) = _Server;

    return 0;
}

int TSLServer_Accept(TSLServer* _Server, uint64_t _MonTime) {
    if (_MonTime >= _Server->recent_connections_time + TSLServer_MAX_CONNECTIONS_WINDOW_SECONDS * 1000) {
        _Server->recent_connections = 0;
        _Server->recent_connections_time = _MonTime;
    }

    if (_Server->recent_connections >= TSLServer_MAX_CONNECTIONS_PER_WINDOW) { return -1; }

    int socket_fd = accept(_Server->listen_fd, NULL, NULL);
    if (socket_fd < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) return 0; // ingen ny klient

        perror("accept");
        return -1;
    }

    TSLServer_Nonblocking(socket_fd);

    int result = _Server->onAccept(socket_fd, _Server->context);
    if (result != 0) close(socket_fd);

    _Server->recent_connections++;
    return 0;
}

void TSLServer_TaskWork(void* _Context, uint64_t _MonTime) {
    TSLServer* _Server = (TSLServer*)_Context;

    TSLServer_Accept(_Server, _MonTime);
}

void TSLServer_Dispose(TSLServer* _Server) {
    mbedtls_pk_free(&_Server->pkey);
    mbedtls_x509_crt_free(&_Server->cert);
    mbedtls_ssl_config_free(&_Server->conf);
    mbedtls_ctr_drbg_free(&_Server->ctr_drbg);
    mbedtls_entropy_free(&_Server->entropy);

    smw_destroyTask(_Server->task);
}

void TSLServer_DisposePtr(TSLServer** _ServerPtr) {
    if (_ServerPtr == NULL || *(_ServerPtr) == NULL) return;

    TSLServer_Dispose(*(_ServerPtr));
    free(*(_ServerPtr));
    *(_ServerPtr) = NULL;
}

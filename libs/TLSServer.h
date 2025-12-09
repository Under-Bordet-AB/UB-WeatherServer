#ifndef __TLSServer_h_
#define __TLSServer_h_

#define _POSIX_C_SOURCE 200809L
#include "../global_defines.h"
#include "smw.h"
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "mbedtls/ctr_drbg.h"
#include "mbedtls/debug.h"
#include "mbedtls/entropy.h"
#include "mbedtls/net_sockets.h"
#include "mbedtls/ssl.h"

typedef int (*TLSServer_OnAccept)(int client_fd, void* context);

typedef struct {
    int listen_fd;

    int recent_connections;
    uint64_t recent_connections_time;

    TLSServer_OnAccept onAccept;
    void* context;

    smw_task* task;

    mbedtls_entropy_context entropy;   // randomness pool
    mbedtls_ctr_drbg_context ctr_drbg; // RNG generator
    mbedtls_ssl_config conf;
    mbedtls_x509_crt cert;
    mbedtls_pk_context pkey;

} TLSServer;

int TLSServer_Initiate(TLSServer* _Server, const char* _Port, TLSServer_OnAccept _OnAccept, void* _Context);
int TLSServer_InitiatePtr(const char* _Port, TLSServer_OnAccept _OnAccept, void* _Context, TLSServer** _ServerPtr);

void TLSServer_Dispose(TLSServer* _Server);
void TLSServer_DisposePtr(TLSServer** _ServerPtr);

static inline int TLSServer_Nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return -1;

    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

#endif //__TLSServer_h_
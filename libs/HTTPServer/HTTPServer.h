
#ifndef __HTTPServer_h_
#define __HTTPServer_h_

#include "../TCPServer.h"
#include "HTTPServerConnection.h"
#include "smw.h"

#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/pk.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509.h>

typedef int (*HTTPServer_OnConnection)(void* _Context, HTTPServerConnection* _Connection);

typedef struct {
    HTTPServer_OnConnection onConnection;
    void* userContext; // User context passed to onConnection callback

    TCPServer tcpServer;
    smw_task* task;

    int use_tls;                       // 1 if this server uses TLS, 0 for plain TCP
    mbedtls_ssl_config ssl_config;     // Shared SSL config for all connections
    mbedtls_entropy_context entropy;   // Entropy pool
    mbedtls_ctr_drbg_context ctr_drbg; // Random number generator
    mbedtls_x509_crt srvcert;          // Server certificate
    mbedtls_pk_context pkey;           // Server private key

} HTTPServer;

int HTTPServer_Initiate(HTTPServer* _Server, HTTPServer_OnConnection _OnConnection);
int HTTPServer_InitiatePtr(HTTPServer_OnConnection _OnConnection, HTTPServer** _ServerPtr);

int HTTPServer_SetUserContext(HTTPServer* _Server, void* userContext);

int HTTPServer_InitiateTLS(HTTPServer* _Server, HTTPServer_OnConnection _OnConnection, const char* cert_path, const char* key_path);
int HTTPServer_InitiateTLSPtr(HTTPServer_OnConnection _OnConnection, HTTPServer** _ServerPtr, const char* cert_path, const char* key_path);

void HTTPServer_Dispose(HTTPServer* _Server);
void HTTPServer_DisposePtr(HTTPServer** _ServerPtr);

#endif //__HTTPServer_h_

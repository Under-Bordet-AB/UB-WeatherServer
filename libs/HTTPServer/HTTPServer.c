#include "HTTPServer.h"
#include <stdlib.h>
#include <string.h>

#include "../../global_defines.h"

#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/pk.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509.h>

//-----------------Internal Functions-----------------

void HTTPServer_TaskWork(void* _Context, uint64_t _MonTime);
int HTTPServer_OnAccept(int _FD, void* _Context);

//----------------------------------------------------

int HTTPServer_Initiate(HTTPServer* _Server, HTTPServer_OnConnection _OnConnection) {
    // Initialize essential fields
    _Server->onConnection = _OnConnection;
    _Server->userContext = NULL;
    _Server->use_tls = 0;
    _Server->task = NULL;

    // Use centralized listen port for easy configuration during testing
    TCPServer_Initiate(&_Server->tcpServer, WeatherServer_TCP_LISTEN_PORT, HTTPServer_OnAccept, _Server);

    _Server->task = smw_createTask(_Server, HTTPServer_TaskWork);

    printf("Info: HTTP server started on port %s\n", WeatherServer_TCP_LISTEN_PORT);
    return 0;
}

int HTTPServer_SetUserContext(HTTPServer* _Server, void* userContext) {
    if (_Server == NULL) return -1;
    _Server->userContext = userContext;
    return 0;
}

int HTTPServer_InitiatePtr(HTTPServer_OnConnection _OnConnection, HTTPServer** _ServerPtr) {
    if (_ServerPtr == NULL) return -1;

    HTTPServer* _Server = (HTTPServer*)malloc(sizeof(HTTPServer));
    if (_Server == NULL) return -2;

    int result = HTTPServer_Initiate(_Server, _OnConnection);
    if (result != 0) {
        free(_Server);
        return result;
    }

    *(_ServerPtr) = _Server;

    return 0;
}

int HTTPServer_InitiateTLS(HTTPServer* _Server, HTTPServer_OnConnection _OnConnection, const char* cert_path, const char* key_path) {
    if (_Server == NULL || cert_path == NULL || key_path == NULL) return -1;

    // Initialize essential fields (don't memset - TLS structures are large)
    _Server->onConnection = _OnConnection;
    _Server->userContext = NULL; // Set to NULL by default
    _Server->use_tls = 1;
    _Server->task = NULL;

    // Initialize TLS components
    mbedtls_entropy_init(&_Server->entropy);
    mbedtls_ctr_drbg_init(&_Server->ctr_drbg);
    mbedtls_x509_crt_init(&_Server->srvcert);
    mbedtls_pk_init(&_Server->pkey);
    mbedtls_ssl_config_init(&_Server->ssl_config);

    // Seed RNG
    const char* pers = "weather_server";
    int ret = mbedtls_ctr_drbg_seed(&_Server->ctr_drbg, mbedtls_entropy_func, &_Server->entropy, (const uint8_t*)pers, strlen(pers));
    if (ret != 0) {
        printf("mbedtls_ctr_drbg_seed failed: %d\n", ret);
        return -2;
    }

    // Load certificate and key
    ret = mbedtls_x509_crt_parse_file(&_Server->srvcert, cert_path);
    if (ret != 0) {
        printf("Failed to load certificate from %s: %d\n", cert_path, ret);
        return -3;
    }

    ret = mbedtls_pk_parse_keyfile(&_Server->pkey, key_path, NULL);
    if (ret != 0) {
        printf("Failed to load key from %s: %d\n", key_path, ret);
        return -4;
    }

    // Setup SSL config
    ret = mbedtls_ssl_config_defaults(&_Server->ssl_config, MBEDTLS_SSL_IS_SERVER, MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT);
    if (ret != 0) {
        printf("mbedtls_ssl_config_defaults failed: %d\n", ret);
        return -5;
    }

    mbedtls_ssl_conf_rng(&_Server->ssl_config, mbedtls_ctr_drbg_random, &_Server->ctr_drbg);
    mbedtls_ssl_conf_ca_chain(&_Server->ssl_config, _Server->srvcert.next, NULL);

    ret = mbedtls_ssl_conf_own_cert(&_Server->ssl_config, &_Server->srvcert, &_Server->pkey);
    if (ret != 0) {
        printf("mbedtls_ssl_conf_own_cert failed: %d\n", ret);
        return -6;
    }

    // Use TLS listen port
    TCPServer_Initiate(&_Server->tcpServer, WeatherServer_TLS_LISTEN_PORT, HTTPServer_OnAccept, _Server);

    _Server->task = smw_createTask(_Server, HTTPServer_TaskWork);

    printf("Info: TLS server started on port %s\n", WeatherServer_TLS_LISTEN_PORT);
    return 0;
}

int HTTPServer_InitiateTLSPtr(HTTPServer_OnConnection _OnConnection, HTTPServer** _ServerPtr, const char* cert_path, const char* key_path) {
    if (_ServerPtr == NULL) return -1;

    HTTPServer* _Server = (HTTPServer*)malloc(sizeof(HTTPServer));
    if (_Server == NULL) return -2;

    int result = HTTPServer_InitiateTLS(_Server, _OnConnection, cert_path, key_path);
    if (result != 0) {
        free(_Server);
        return result;
    }

    *(_ServerPtr) = _Server;

    return 0;
}

int HTTPServer_OnAccept(int _FD, void* _Context) {
    HTTPServer* _Server = (HTTPServer*)_Context;

    HTTPServerConnection* connection = NULL;
    int result = HTTPServerConnection_InitiatePtr(_FD, &connection);
    if (result != 0) {
        printf("HTTPServer_OnAccept: Failed to initiate connection\n");
        return -1;
    }

    // Set TLS if enabled on this server
    if (_Server->use_tls) { HTTPServerConnection_SetTLS(connection, 1, &_Server->ssl_config, &_Server->ctr_drbg); }

    // Use userContext if set, otherwise pass the HTTPServer
    void* context = _Server->userContext != NULL ? _Server->userContext : (void*)_Server;
    _Server->onConnection(context, connection);

    return 0;
}

void HTTPServer_TaskWork(void* _Context, uint64_t _MonTime) {
    // HTTPServer* _Server = (HTTPServer*)_Context;
}

void HTTPServer_Dispose(HTTPServer* _Server) {
    TCPServer_Dispose(&_Server->tcpServer);
    smw_destroyTask(_Server->task);

    // Clean up TLS resources if initialized
    if (_Server->use_tls) {
        mbedtls_pk_free(&_Server->pkey);
        mbedtls_x509_crt_free(&_Server->srvcert);
        mbedtls_ssl_config_free(&_Server->ssl_config);
        mbedtls_ctr_drbg_free(&_Server->ctr_drbg);
        mbedtls_entropy_free(&_Server->entropy);
    }
}

void HTTPServer_DisposePtr(HTTPServer** _ServerPtr) {
    if (_ServerPtr == NULL || *(_ServerPtr) == NULL) return;

    HTTPServer_Dispose(*(_ServerPtr));
    free(*(_ServerPtr));
    *(_ServerPtr) = NULL;
}
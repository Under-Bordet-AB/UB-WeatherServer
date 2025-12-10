#include "HTTPServerConnection.h"
#include <errno.h>
#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <mbedtls/ctr_drbg.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/ssl.h>

//-----------------Custom BIO Callbacks for non-blocking sockets-----------------

// Custom BIO send callback - writes to a non-blocking socket
static int bio_send(void* ctx, const unsigned char* buf, size_t len) {
    int fd = (int)(intptr_t)ctx;
    int ret = send(fd, buf, len, MSG_NOSIGNAL);

    if (ret < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) { return MBEDTLS_ERR_SSL_WANT_WRITE; }
        return MBEDTLS_ERR_NET_SEND_FAILED;
    }

    return ret;
}

// Custom BIO recv callback - reads from a non-blocking socket
static int bio_recv(void* ctx, unsigned char* buf, size_t len) {
    int fd = (int)(intptr_t)ctx;
    int ret = recv(fd, buf, len, MSG_DONTWAIT);

    if (ret < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) { return MBEDTLS_ERR_SSL_WANT_READ; }
        return MBEDTLS_ERR_NET_RECV_FAILED;
    }

    if (ret == 0) {
        // Connection closed
        return MBEDTLS_ERR_NET_CONN_RESET;
    }

    return ret;
}

//----------------------------------------------------//-----------------Internal Functions-----------------

void HTTPServerConnection_TaskWork(void* _Context, uint64_t _MonTime);

//----------------------------------------------------

int HTTPServerConnection_Initiate(HTTPServerConnection* _Connection, int _FD) {
    TCPClient_Initiate(&_Connection->tcpClient, _FD);

    _Connection->url = NULL;
    _Connection->method = NULL;
    _Connection->bytesRead = 0;
    _Connection->state = HTTPServerConnection_State_Init;
    _Connection->startTime = 0;
    _Connection->writeBuffer = NULL;
    _Connection->readBuffer[0] = '\0';
    _Connection->bytesSent = 0;

    _Connection->use_tls = 0;
    _Connection->ssl_config = NULL;
    _Connection->tls_rng = NULL;

    _Connection->task = smw_createTask(_Connection, HTTPServerConnection_TaskWork);

    return 0;
}

int HTTPServerConnection_InitiatePtr(int _FD, HTTPServerConnection** _ConnectionPtr) {
    if (_ConnectionPtr == NULL) return -1;

    HTTPServerConnection* _Connection = (HTTPServerConnection*)malloc(sizeof(HTTPServerConnection));
    if (_Connection == NULL) return -2;

    int result = HTTPServerConnection_Initiate(_Connection, _FD);
    if (result != 0) {
        free(_Connection);
        return result;
    }

    *(_ConnectionPtr) = _Connection;

    return 0;
}

void HTTPServerConnection_SetCallback(HTTPServerConnection* _Connection, void* _Context, HTTPServerConnection_OnRequest _OnRequest) {

    _Connection->bytesSent = 0;
    _Connection->context = _Context;
    _Connection->onRequest = _OnRequest;
}

int HTTPServerConnection_SetTLS(HTTPServerConnection* _Connection, int use_tls, void* ssl_config, void* tls_rng) {
    if (_Connection == NULL) return -1;

    _Connection->use_tls = use_tls;
    _Connection->ssl_config = ssl_config;
    _Connection->tls_rng = tls_rng;

    return 0;
}

void HTTPServerConnection_SendResponse_Binary(HTTPServerConnection* _Connection, int _responseCode, uint8_t* _responseBody, size_t _responseBodySize,
                                              char* _contentType) {

    if (_Connection->state != HTTPServerConnection_State_Wait) return;
    int isRedirect = (_responseCode == 301 || _responseCode == 302);
    HTTPResponse* resp = HTTPResponse_new(_responseCode, isRedirect ? NULL : _responseBody, isRedirect ? 0 : _responseBodySize);
    if (_contentType != NULL) HTTPResponse_add_header(resp, "Content-Type", _contentType);
    if (isRedirect) HTTPResponse_add_header(resp, "Location", (const char*)_responseBody);
    size_t messageSize = 0;
    char* message = (char*)HTTPResponse_tostring(resp, &messageSize);
    _Connection->writeBuffer = (uint8_t*)message;
    _Connection->writeBufferSize = messageSize;
    HTTPResponse_Dispose(&resp);
    _Connection->state = HTTPServerConnection_State_Send;
}

void HTTPServerConnection_SendResponse(HTTPServerConnection* _Connection, int _responseCode, char* _responseBody, char* _contentType) {

    if (_Connection->state != HTTPServerConnection_State_Wait) return;

    HTTPServerConnection_SendResponse_Binary(_Connection, _responseCode, (uint8_t*)_responseBody, strlen(_responseBody), _contentType);
}

void HTTPServerConnection_TaskWork(void* _Context, uint64_t _MonTime) {
    HTTPServerConnection* _Connection = (HTTPServerConnection*)_Context;

    if (_Connection->state != HTTPServerConnection_State_Init && _MonTime - _Connection->startTime >= HTTPSERVER_TIMEOUT_MS) {
        _Connection->state = HTTPServerConnection_State_Dispose;
    }

    switch (_Connection->state) {
    case HTTPServerConnection_State_Init: {
        _Connection->startTime = _MonTime;
        if (_Connection->use_tls) {
            _Connection->state = HTTPServerConnection_State_TLS_Handshake;
        } else {
            _Connection->state = HTTPServerConnection_State_Reading;
        }
        break;
    }
    case HTTPServerConnection_State_TLS_Handshake: {
        // Initialize SSL context on first entry to this state
        if (_Connection->tcpClient.ssl == NULL && _Connection->ssl_config != NULL) {
            mbedtls_ssl_context* ssl = (mbedtls_ssl_context*)malloc(sizeof(mbedtls_ssl_context));
            if (ssl == NULL) {
                _Connection->state = HTTPServerConnection_State_Failed;
                break;
            }

            mbedtls_ssl_init(ssl);
            int ret = mbedtls_ssl_setup(ssl, (mbedtls_ssl_config*)_Connection->ssl_config);
            if (ret != 0) {
                printf("mbedtls_ssl_setup failed: %d\n", ret);
                free(ssl);
                _Connection->state = HTTPServerConnection_State_Failed;
                break;
            }

            // Set socket file descriptor as BIO with custom callbacks
            mbedtls_ssl_set_bio(ssl, (void*)(intptr_t)_Connection->tcpClient.fd, bio_send, bio_recv, NULL);

            TCPClient_SetSSL(&_Connection->tcpClient, ssl);
        }

        // Perform handshake
        if (_Connection->tcpClient.ssl != NULL) {
            int ret = TCPClient_TLS_Handshake(&_Connection->tcpClient);
            if (ret == 0) {
                // Handshake successful
                _Connection->state = HTTPServerConnection_State_Reading;
            } else if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
                // Retry next iteration (non-blocking)
            } else {
                // Error
                printf("TLS handshake error: %d\n", ret);
                _Connection->state = HTTPServerConnection_State_Failed;
            }
        }
        break;
    }
    case HTTPServerConnection_State_Reading: {
        TCPClient* tcpClient = &_Connection->tcpClient;
        int read = 0;
        int read_amount = READBUFFER_SIZE - _Connection->bytesRead - 1;
        if (read_amount > 0) {
            read = TCPClient_Read(tcpClient, (uint8_t*)(_Connection->readBuffer + _Connection->bytesRead), read_amount);

            if (read > 0) {
                _Connection->bytesRead += read;
                _Connection->readBuffer[_Connection->bytesRead] = '\0';
            }
        }

        char* ret = strstr(_Connection->readBuffer, "\r\n\r\n");
        if (ret != NULL) {
            _Connection->state = HTTPServerConnection_State_Parsing;
        } else if (read == 0) {
            if (_Connection->bytesRead + 1 >= READBUFFER_SIZE) {
                printf("Request overflows readBuffer, dropping.\n");
                HTTPServerConnection_SendResponse(_Connection, 413, "", NULL);
            } else {
                printf("Request is incomplete, dropping.\n");
                HTTPServerConnection_SendResponse(_Connection, 400, "", NULL);
            }
            _Connection->state = HTTPServerConnection_State_Dispose;
        }

        break;
    }
    case HTTPServerConnection_State_Parsing: {
        HTTPRequest* request = HTTPRequest_fromstring(_Connection->readBuffer);

        if (request->valid) {
            _Connection->url = strdup(request->URL);
            RequestMethod method = request->method;
            HTTPRequest_Dispose(&request);
            _Connection->method = strdup(RequestMethod_tostring(method));
            _Connection->state = HTTPServerConnection_State_Wait;
            if (method == GET) {
                _Connection->onRequest(_Connection->context);
            } else if (method == OPTIONS) {
                printf("Responding to preflight request for %s\n", _Connection->url);
                HTTPServerConnection_SendResponse(_Connection, 204, "", NULL);
            } else {
                printf("Unsupported request type '%s' received for %s\n", _Connection->method, _Connection->url);
                HTTPServerConnection_SendResponse(_Connection, 405, "Method unsupported", "text/plain");
            }
        } else {
            _Connection->state = HTTPServerConnection_State_Wait;
            printf("Dropping invalid request, reason: %s\n", InvalidReason_tostring(request->reason));
            HTTPRequest_Dispose(&request);
            HTTPServerConnection_SendResponse(_Connection, 400, "Invalid request received", "text/plain");
        }

        break;
    }
    case HTTPServerConnection_State_Send: {
        if (_Connection->writeBuffer == NULL) {
            _Connection->state = HTTPServerConnection_State_Failed;
            break;
        }
        int n = TCPClient_Write(&_Connection->tcpClient, (uint8_t*)(_Connection->writeBuffer + _Connection->bytesSent),
                                _Connection->writeBufferSize - _Connection->bytesSent);
        if (n > 0) { _Connection->bytesSent += n; }

        if (_Connection->bytesSent == _Connection->writeBufferSize) {
            _Connection->state = HTTPServerConnection_State_Dispose; // Formerly set to Wait
        }
        break;
    }
    case HTTPServerConnection_State_Wait: {
        break;
    }
    case HTTPServerConnection_State_Timeout: {
        printf("Connection timed out\n");
        _Connection->state = HTTPServerConnection_State_Dispose;
        break;
    }
    case HTTPServerConnection_State_Done: {
        // Ska den verkligen disposea här?
        _Connection->state = HTTPServerConnection_State_Dispose;
        break;
    }
    case HTTPServerConnection_State_Dispose: {
        // Don't dispose here - just stop the task
        // The connection will be disposed when the server shuts down
        smw_destroyTask(_Connection->task);
        _Connection->task = NULL;
        return; // Exit the task work
    }
    case HTTPServerConnection_State_Failed: {
        printf("Reading failed\n");
        _Connection->state = HTTPServerConnection_State_Dispose;
        break;
    }
    default: {
        printf("Unsupported state\n");
        break;
    }
    }

    // printf("HTTPServerConnection_TaskWork\n");
}

void HTTPServerConnection_Dispose(HTTPServerConnection* _Connection) {
    // Clean up SSL context if allocated
    if (_Connection->tcpClient.ssl != NULL) {
        mbedtls_ssl_free(_Connection->tcpClient.ssl);
        free(_Connection->tcpClient.ssl);
        _Connection->tcpClient.ssl = NULL;
    }

    TCPClient_Dispose(&_Connection->tcpClient);
    smw_destroyTask(_Connection->task);

    if (_Connection->writeBuffer) free(_Connection->writeBuffer);

    if (_Connection->url) free(_Connection->url);

    if (_Connection->method) free(_Connection->method);
}

void HTTPServerConnection_DisposePtr(HTTPServerConnection** _ConnectionPtr) {
    if (_ConnectionPtr == NULL || *(_ConnectionPtr) == NULL) return;

    HTTPServerConnection_Dispose(*(_ConnectionPtr));
    free(*(_ConnectionPtr));
    *(_ConnectionPtr) = NULL;
}
#include "HTTPServerConnection.h"
#include <stdio.h>
#include <stdlib.h>

//-----------------Internal Functions-----------------

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
    _Connection->bytesSent = 0;

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

void HTTPServerConnection_SendResponse_Binary(HTTPServerConnection* _Connection,
                                              int _responseCode,
                                              uint8_t* _responseBody,
                                              size_t _responseBodySize,
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
        _Connection->state = HTTPServerConnection_State_Reading;
        break;
    }
    case HTTPServerConnection_State_Reading: {
        TCPClient *tcpClient = &_Connection->tcpClient;
        int read = 0;
        int read_amount = READBUFFER_SIZE - _Connection->bytesRead - 1;
        if(read_amount > 0)
        {
        read = TCPClient_Read(
            tcpClient,
            (uint8_t *)(_Connection->readBuffer + _Connection->bytesRead),
            read_amount);

        if (read > 0) {
            _Connection->bytesRead += read;
            _Connection->readBuffer[_Connection->bytesRead] = '\0';
        }
        }

        char *ret = strstr(_Connection->readBuffer, "\r\n\r\n");
        if (ret != NULL) {
        _Connection->state = HTTPServerConnection_State_Parsing;
        } else if(read == 0) {
        if(_Connection->bytesRead + 1 >= READBUFFER_SIZE)
        {
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
        HTTPRequest *request = HTTPRequest_fromstring(_Connection->readBuffer);

        if(request->valid)
        {
            _Connection->url = strdup(request->URL);
            RequestMethod method = request->method;
            HTTPRequest_Dispose(&request);
            _Connection->method = strdup(RequestMethod_tostring(method));
            _Connection->state = HTTPServerConnection_State_Wait;
            if (method == GET) {
                _Connection->onRequest(_Connection->context);
            } else if(method == OPTIONS) {
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
        HTTPServerConnection_Dispose(_Connection);
        break;
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
    TCPClient_Dispose(&_Connection->tcpClient);
    if (_Connection->writeBuffer) free(_Connection->writeBuffer);
    smw_destroyTask(_Connection->task);
}

void HTTPServerConnection_DisposePtr(HTTPServerConnection** _ConnectionPtr) {
    if (_ConnectionPtr == NULL || *(_ConnectionPtr) == NULL) return;

    HTTPServerConnection_Dispose(*(_ConnectionPtr));
    free(*(_ConnectionPtr));
    *(_ConnectionPtr) = NULL;
}
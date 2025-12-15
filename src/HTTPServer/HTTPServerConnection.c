#include "../../include/HTTPServer/HTTPServerConnection.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//-----------------Internal Functions-----------------
void HTTPServerConnection_TaskWork(void *_Context, uint64_t _MonTime);
//----------------------------------------------------

int HTTPServerConnection_Initiate(HTTPServerConnection *_Connection, conn_t *_Conn) {
  // Store the connection object. HTTPServerConnection now OWNS this object.
  _Connection->conn = _Conn;

  _Connection->url = NULL;
  _Connection->method = NULL;
  _Connection->bytesRead = 0;
  _Connection->state = HTTPServerConnection_State_Init;
  _Connection->startTime = 0;
  _Connection->writeBuffer = NULL;
  _Connection->readBuffer[0] = '\0';
  _Connection->bytesSent = 0;
  _Connection->task = smw_createTask(_Connection, HTTPServerConnection_TaskWork);

  return 0;
}

int HTTPServerConnection_InitiatePtr(conn_t *_Conn, HTTPServerConnection **_ConnectionPtr) {
  if (_ConnectionPtr == NULL) return -1;
  
  HTTPServerConnection *_Connection = (HTTPServerConnection *)malloc(sizeof(HTTPServerConnection));
  if (_Connection == NULL) return -2;

  int result = HTTPServerConnection_Initiate(_Connection, _Conn);
  if (result != 0) {
    free(_Connection);
    return result;
  }

  *(_ConnectionPtr) = _Connection;
  return 0;
}

void HTTPServerConnection_SetCallback(HTTPServerConnection *_Connection, void *_Context,
                                      HTTPServerConnection_OnRequest _OnRequest) {
  _Connection->bytesSent = 0;
  _Connection->context = _Context;
  _Connection->onRequest = _OnRequest;
}

void HTTPServerConnection_SendResponse_Binary(HTTPServerConnection *_Connection,
                                       int _responseCode, uint8_t *_responseBody, size_t _responseBodySize, char *_contentType) {
  if (_Connection->state != HTTPServerConnection_State_Wait) return;
  
  int isRedirect = (_responseCode == 301 || _responseCode == 302);
  HTTPResponse *resp = HTTPResponse_new(_responseCode, isRedirect ? NULL : _responseBody, isRedirect ? 0 : _responseBodySize);
  
  if(_contentType != NULL)
    HTTPResponse_add_header(resp, "Content-Type", _contentType);
  if(isRedirect)
    HTTPResponse_add_header(resp, "Location", (const char*)_responseBody);
    
  size_t messageSize = 0;
  char *message = (char*)HTTPResponse_tostring(resp, &messageSize);
  _Connection->writeBuffer = (uint8_t *)message;
  _Connection->writeBufferSize = messageSize;
  HTTPResponse_Dispose(&resp);
  _Connection->state = HTTPServerConnection_State_Send;
}

void HTTPServerConnection_SendResponse(HTTPServerConnection *_Connection,
                                       int _responseCode, char *_responseBody, char *_contentType) {
  if (_Connection->state != HTTPServerConnection_State_Wait) return;
  HTTPServerConnection_SendResponse_Binary(_Connection, _responseCode, (uint8_t*)_responseBody, strlen(_responseBody), _contentType);
}

void HTTPServerConnection_TaskWork(void *_Context, uint64_t _MonTime) {
  HTTPServerConnection *_Connection = (HTTPServerConnection *)_Context;
  
  if (_Connection->state != HTTPServerConnection_State_Init &&
      _MonTime - _Connection->startTime >= HTTPSERVER_TIMEOUT_MS) {
    _Connection->state = HTTPServerConnection_State_Dispose;
  }

  switch (_Connection->state) {
  case HTTPServerConnection_State_Init: {
    _Connection->startTime = _MonTime;
    _Connection->state = HTTPServerConnection_State_Reading;
    break;
  }
  case HTTPServerConnection_State_Reading: {
    int read = 0;
    int read_amount = READBUFFER_SIZE - _Connection->bytesRead - 1;
    if(read_amount > 0)
    {
      read = _Connection->conn->vtable->read(_Connection->conn, 
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
       // Wait for more data (non-blocking return 0)
       // OR if closed: 
       // Note: Standard recv returns 0 on close, -1 on error. 
       // Your vtables need to return -1 on error/close and 0 on EWOULDBLOCK usually.
       // Based on your connection.c, read returns 0 on WANT_READ.
    }
    // If connection closed or error (read < 0)
    if (read < 0) {
         _Connection->state = HTTPServerConnection_State_Dispose;
    }
    break;
  }
  case HTTPServerConnection_State_Parsing: {
     // ... (Existing parsing logic remains the same) ...
    HTTPRequest *request = HTTPRequest_fromstring(_Connection->readBuffer);
    if(request->valid) {
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
    int n = _Connection->conn->vtable->write(_Connection->conn,
        (uint8_t *)(_Connection->writeBuffer + _Connection->bytesSent),
        _Connection->writeBufferSize - _Connection->bytesSent);
        
    if (n > 0) {
      _Connection->bytesSent += n;
    }

    if (_Connection->bytesSent == _Connection->writeBufferSize) {
      _Connection->state = HTTPServerConnection_State_Dispose;
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
    _Connection->state = HTTPServerConnection_State_Dispose;
    break;
  }
  case HTTPServerConnection_State_Dispose: {
    smw_destroyTask(_Connection->task);
    _Connection->task = NULL;
    return;
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
}

void HTTPServerConnection_Dispose(HTTPServerConnection *_Connection) {
  if (_Connection->conn) {
      _Connection->conn->vtable->close(_Connection->conn);
      _Connection->conn = NULL; 
  }
  
  smw_destroyTask(_Connection->task);

  if (_Connection->writeBuffer) free(_Connection->writeBuffer);
  if (_Connection->url) free(_Connection->url);
  if (_Connection->method) free(_Connection->method);
}

void HTTPServerConnection_DisposePtr(HTTPServerConnection **_ConnectionPtr) {
  if (_ConnectionPtr == NULL || *(_ConnectionPtr) == NULL) return;
  HTTPServerConnection_Dispose(*(_ConnectionPtr));
  free(*(_ConnectionPtr));
  *(_ConnectionPtr) = NULL;
}

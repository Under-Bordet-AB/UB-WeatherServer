
#ifndef __HTTPServerConnection_h_
#define __HTTPServerConnection_h_

#include "../TCPClient.h"
#include "HTTPParser.h"
#include "smw.h"

#include "global_defines.h"

typedef int (*HTTPServerConnection_OnRequest)(void *_Context);

typedef enum {
  HTTPServerConnection_State_Init,
  HTTPServerConnection_State_Reading,
  HTTPServerConnection_State_Parsing,
  HTTPServerConnection_State_Wait,
  HTTPServerConnection_State_Timeout,
  HTTPServerConnection_State_Send,
  HTTPServerConnection_State_Done,
  HTTPServerConnection_State_Dispose,
  HTTPServerConnection_State_Failed
} HTTPServerConnection_State;

#define READBUFFER_SIZE HTTPServerConnection_READBUFFER_SIZE // From global_defines.h (original: libs/HTTPServer/HTTPServerConnection.h)
#define WRITEBUFFER_SIZE HTTPServerConnection_WRITEBUFFER_SIZE // From global_defines.h (original: libs/HTTPServer/HTTPServerConnection.h)
#define HTTPSERVER_TIMEOUT_MS HTTPServerConnection_HTTPSERVER_TIMEOUT_MS // From global_defines.h (original: libs/HTTPServer/HTTPServerConnection.h)

typedef struct {
  TCPClient tcpClient;
  char readBuffer[READBUFFER_SIZE];
  int bytesRead;
  uint8_t *writeBuffer;
  int writeBufferSize;
  int bytesSent;
  uint64_t startTime;

  void *context;
  HTTPServerConnection_OnRequest onRequest;

  char *method;
  char *url;

  smw_task *task;
  HTTPServerConnection_State state;
} HTTPServerConnection;

int HTTPServerConnection_Initiate(HTTPServerConnection *_Connection, int _FD);
int HTTPServerConnection_InitiatePtr(int _FD,
                                     HTTPServerConnection **_ConnectionPtr);

void HTTPServerConnection_SetCallback(
    HTTPServerConnection *_Connection, void *_Context,
    HTTPServerConnection_OnRequest _OnRequest);
void HTTPServerConnection_SendResponse(HTTPServerConnection *_Connection,
                                       int _responseCode, char *_responseBody, char *_contentType);
void HTTPServerConnection_SendResponse_Binary(HTTPServerConnection *_Connection,
                                       int _responseCode, uint8_t *_responseBody, size_t _responseBodySize, char *_contentType);

void HTTPServerConnection_Dispose(HTTPServerConnection *_Connection);
void HTTPServerConnection_DisposePtr(HTTPServerConnection **_ConnectionPtr);

#endif //__HTTPServerConnection_h_
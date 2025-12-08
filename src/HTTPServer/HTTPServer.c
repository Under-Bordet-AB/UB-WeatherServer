#include "HTTPServer/HTTPServer.h"
#include <stdlib.h>

#include "global_defines.h"

//-----------------Internal Functions-----------------

void HTTPServer_TaskWork(void *_Context, uint64_t _MonTime);
int HTTPServer_OnAccept(int _FD, void *_Context);

//----------------------------------------------------

int HTTPServer_Initiate(HTTPServer *_Server,
                        HTTPServer_OnConnection _OnConnection, char *port) {
  _Server->onConnection = _OnConnection;

  // Use centralized listen port for easy configuration during testing
  TCPServer_Initiate(&_Server->tcpServer, port, HTTPServer_OnAccept, _Server);

  _Server->task = smw_createTask(_Server, HTTPServer_TaskWork);

  return 0;
}

int HTTPServer_InitiatePtr(HTTPServer_OnConnection _OnConnection,
                           HTTPServer **_ServerPtr, char *port) {
  if (_ServerPtr == NULL)
    return -1;

  HTTPServer *_Server = (HTTPServer *)malloc(sizeof(HTTPServer));
  if (_Server == NULL)
    return -2;

  int result = HTTPServer_Initiate(_Server, _OnConnection, port);
  if (result != 0) {
    free(_Server);
    return result;
  }

  *(_ServerPtr) = _Server;

  return 0;
}

int HTTPServer_OnAccept(int _FD, void *_Context) {
  HTTPServer *_Server = (HTTPServer *)_Context;

  HTTPServerConnection *connection = NULL;
  int result = HTTPServerConnection_InitiatePtr(_FD, &connection);
  if (result != 0) {
    printf("HTTPServer_OnAccept: Failed to initiate connection\n");
    return -1;
  }

  _Server->onConnection(_Server, connection);

  return 0;
}

void HTTPServer_TaskWork(void *_Context, uint64_t _MonTime) {
  // HTTPServer* _Server = (HTTPServer*)_Context;
}

void HTTPServer_Dispose(HTTPServer *_Server) {
  TCPServer_Dispose(&_Server->tcpServer);
  smw_destroyTask(_Server->task);
}

void HTTPServer_DisposePtr(HTTPServer **_ServerPtr) {
  if (_ServerPtr == NULL || *(_ServerPtr) == NULL)
    return;

  HTTPServer_Dispose(*(_ServerPtr));
  free(*(_ServerPtr));
  *(_ServerPtr) = NULL;
}

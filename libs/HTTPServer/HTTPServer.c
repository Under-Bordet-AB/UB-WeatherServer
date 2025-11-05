/*
HTTPServer_TaskWork is the scheduler/task callback that should do the small, non-blocking work to advance the protocol layer
(drive connection state machines, do short reads/writes, and call the parser/app callbacks). Put only I/O/coordination there.
Not heavy business logic. Business logic belongs in the app callback (server_session_request_cb / route handlers).

What TaskWork should and should not do (concise)

Should:
    Be quick and non-blocking.
    Iterate active connections and call their per-connection step (e.g., protocol_http_connection_step or connection_step).
    Track next wakeup deadlines returned by connections and update scheduler/timers.
    Clean up closed/errored connections.
    Possibly accept housekeeping or backpressure tasks.
Should not:
    Perform long/blocking operations (DB calls, blocking DNS, long computation).
    Contain application business logic (JSON formation, DB queries). Instead call the app callback or
    spawn a background task for long work.

Where to place your code
Put transport/protocol I/O and short parsing/work in the connection step functions (protocol_http_connection.c).
In HTTPServer_TaskWork call those connection step functions for each connection.
Put route handlers / business logic in app/server.c (server_session_request_cb or server_route_*).
If those need to block, schedule a separate task from the scheduler.
*/

#include "HTTPServer.h"
#include <stdlib.h>

//-----------------Internal Functions-----------------

void HTTPServer_TaskWork(void* _Context, uint64_t _MonTime);
int HTTPServer_OnAccept(int _FD, void* _Context);

//----------------------------------------------------

int HTTPServer_Initiate(HTTPServer* _Server, HTTPServer_OnConnection _OnConnection) {
    _Server->onConnection = _OnConnection;

    TCPServer_Initiate(&_Server->tcpServer, "8080", HTTPServer_OnAccept, _Server);

    _Server->task = smw_createTask(_Server, HTTPServer_TaskWork);

    return 0;
}

int HTTPServer_InitiatePtr(HTTPServer_OnConnection _OnConnection, HTTPServer** _ServerPtr) {
    if (_ServerPtr == NULL)
        return -1;

    HTTPServer* _Server = (HTTPServer*)malloc(sizeof(HTTPServer));
    if (_Server == NULL)
        return -2;

    int result = HTTPServer_Initiate(_Server, _OnConnection);
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

    _Server->onConnection(_Server, connection);

    return 0;
}

void HTTPServer_TaskWork(void* _Context, uint64_t _MonTime) {
    printf("HTTPServer taskwork started\n");
}

void HTTPServer_Dispose(HTTPServer* _Server) {
    TCPServer_Dispose(&_Server->tcpServer);
    smw_destroyTask(_Server->task);
}

void HTTPServer_DisposePtr(HTTPServer** _ServerPtr) {
    if (_ServerPtr == NULL || *(_ServerPtr) == NULL)
        return;

    HTTPServer_Dispose(*(_ServerPtr));
    free(*(_ServerPtr));
    *(_ServerPtr) = NULL;
}

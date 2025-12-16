#include "../../include/HTTPServer/HTTPServer.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "../../global_defines.h" // Assumed to contain TLS_PORT macro

//-----------------Internal Functions-----------------

static int HTTPServer_OnAccept(conn_t* new_conn, void *user_ctx);

//----------------------------------------------------

// REVERTED IMPLEMENTATION: Uses the passed 'port' for TCP and TLS_PORT for TLS
int HTTPServer_Initiate(HTTPServer *_Server, void *_Context,
                        HTTPServer_OnConnection _OnConnection, 
                        char *port) { // Single port argument
    
    _Server->onConnection = _OnConnection;
    _Server->context = _Context;
    _Server->tcp_listen_server = NULL;
    _Server->tls_listen_server = NULL;
    
    // 1. Initialize TCP Listener (HTTP) using the passed 'port'
    if (port) {
        _Server->tcp_listen_server = conn_listen_server_tcp_init(port, HTTPServer_OnAccept, _Server);
        if (_Server->tcp_listen_server == NULL) {
            printf("HTTPServer_Initiate: Failed to initialize TCP listener on port %s\n", port);
            // Continue to try and start the TLS server
        }
    }
    
    // 2. Initialize TLS Listener (HTTPS) using the global define TLS_PORT
    _Server->tls_listen_server = conn_listen_server_tls_init(TLS_PORT, HTTPServer_OnAccept, _Server);
    if (_Server->tls_listen_server == NULL) {
        printf("HTTPServer_Initiate: Failed to initialize TLS listener on global port %s\n", TLS_PORT);
        // Clean up the TCP server if TLS failed
        if (_Server->tcp_listen_server) {
            conn_listen_server_dispose(_Server->tcp_listen_server);
            _Server->tcp_listen_server = NULL;
        }
        return -1;
    }
    
    // Final check: did we start at least one server?
    if (!_Server->tcp_listen_server && !_Server->tls_listen_server) {
        printf("HTTPServer_Initiate: Failed to start both TCP (%s) and TLS (%s) listeners.\n", port, TLS_PORT);
        return -1;
    }

    _Server->task = NULL; 

    return 0;
}

// REVERTED SIGNATURE: Uses a single port parameter
int HTTPServer_InitiatePtr(void *_Context, HTTPServer_OnConnection _OnConnection,
                           HTTPServer **_ServerPtr, char *port) { // Single port argument
    if (_ServerPtr == NULL)
        return -1;

    HTTPServer *_Server = (HTTPServer *)malloc(sizeof(HTTPServer));
    if (_Server == NULL)
        return -2;

    // Pass the single port to the main Initiate function
    int result = HTTPServer_Initiate(_Server, _Context, _OnConnection, port);
    if (result != 0) {
        free(_Server);
        return result;
    }

    *(_ServerPtr) = _Server;

    return 0;
}

static int HTTPServer_OnAccept(conn_t* new_conn, void *user_ctx) {
    HTTPServer *_Server = (HTTPServer *)user_ctx;

    HTTPServerConnection *connection = NULL;
    
    int result = HTTPServerConnection_InitiatePtr(new_conn, &connection);
    
    if (result != 0) {
        printf("HTTPServer_OnAccept: Failed to initiate connection\n");
        new_conn->vtable->close(new_conn);
        return -1;
    }

    // Call user callback, passing the stored context and the new connection
    _Server->onConnection(_Server->context, connection);

	return 0;
}

void HTTPServer_Dispose(HTTPServer *_Server) {
    if (_Server->tcp_listen_server) {
        conn_listen_server_dispose(_Server->tcp_listen_server);
        _Server->tcp_listen_server = NULL;
    }
    if (_Server->tls_listen_server) {
        conn_listen_server_dispose(_Server->tls_listen_server);
        _Server->tls_listen_server = NULL;
    }
    
    if (_Server->task) {
        smw_destroyTask(_Server->task);
        _Server->task = NULL;
    }
}

void HTTPServer_DisposePtr(HTTPServer **_ServerPtr) {
    if (_ServerPtr == NULL || *(_ServerPtr) == NULL)
        return;

    HTTPServer_Dispose(*(_ServerPtr));
    free(*(_ServerPtr));
    *(_ServerPtr) = NULL;
}

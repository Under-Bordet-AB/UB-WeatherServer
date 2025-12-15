#ifndef __HTTPServer_h_
#define __HTTPServer_h_

#include "smw.h"
#include "HTTPServerConnection.h"
#include "../connection.h" // Includes conn_listen_server_t

typedef int (*HTTPServer_OnConnection)(void* _Context, HTTPServerConnection* _Connection);

typedef struct
{
    HTTPServer_OnConnection onConnection;
    void *context; // Context pointer for the onConnection callback

    // Separate listeners for TCP (HTTP) and TLS (HTTPS)
    conn_listen_server_t *tcp_listen_server;
    conn_listen_server_t *tls_listen_server; 

    smw_task* task;

} HTTPServer;


// REVERTED SIGNATURES: Now accepts a single port argument. TLS port is from global_defines.h
int HTTPServer_Initiate(HTTPServer* _Server, void *_Context, HTTPServer_OnConnection _OnConnection, char *port);
int HTTPServer_InitiatePtr(void *_Context, HTTPServer_OnConnection _OnConnection, HTTPServer** _ServerPtr, char *port);


void HTTPServer_Dispose(HTTPServer* _Server);
void HTTPServer_DisposePtr(HTTPServer** _ServerPtr);


#endif //__HTTPServer_h_

#ifndef HTTPClient_h
#define HTTPClient_h

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "TCPClient.h"
#include "smw.h"

typedef enum
{
	HTTPClient_State_Init,
	HTTPClient_State_Connect,
	HTTPClient_State_Transmit,
	HTTPClient_State_Receive,
	HTTPClient_State_Close

} HTTPClient_State;

typedef struct HTTPClient HTTPClient;

typedef struct HTTPClient
{
	void (*callback)(HTTPClient* _CLient, const char* _Event);
	uint8_t* buffer;
	uint8_t* bufferPtr;

    HTTPClient_State http_client_state;
    TCPClient tcp_client;
    int message_len;

	char* host;
	char* port;

	int working;

	smw_task* task;

} HTTPClient;

int HTTPClient_Initiate(HTTPClient* _Client);

int HTTPClient_GET(HTTPClient* _Client, const char* _URL, void (*callback)(HTTPClient* _Client, const char* _Event));

void HTTPClient_Dispose(HTTPClient* _Client);

#endif
#include "HTTPServerConnection.h"
#include "HTTPParser.h"
#include <stdlib.h>
#include <stdio.h>

//-----------------Internal Functions-----------------

void HTTPServerConnection_TaskWork(void* _Context, uint64_t _MonTime);

//----------------------------------------------------

int HTTPServerConnection_Initiate(HTTPServerConnection* _Connection, int _FD)
{
	TCPClient_Initiate(&_Connection->tcpClient, _FD);
	
	_Connection->url = NULL;
	_Connection->method = NULL;
	_Connection->bytesRead = 0;
	_Connection->state = HTTPServerConnection_State_Init;
	_Connection->startTime = 0;

	_Connection->task = smw_createTask(_Connection, HTTPServerConnection_TaskWork);

	return 0;
}

int HTTPServerConnection_InitiatePtr(int _FD, HTTPServerConnection** _ConnectionPtr)
{
	if(_ConnectionPtr == NULL)
		return -1;

	HTTPServerConnection* _Connection = (HTTPServerConnection*)malloc(sizeof(HTTPServerConnection));
	if(_Connection == NULL)
		return -2;

	int result = HTTPServerConnection_Initiate(_Connection, _FD);
	if(result != 0)
	{
		free(_Connection);
		return result;
	}

	*(_ConnectionPtr) = _Connection;

	return 0;
}

void HTTPServerConnection_SetCallback(HTTPServerConnection* _Connection, void* _Context, HTTPServerConnection_OnRequest _OnRequest)
{
	_Connection->context = _Context;
	_Connection->onRequest = _OnRequest;
}

void HTTPServerConnection_TaskWork(void* _Context, uint64_t _MonTime)
{
	HTTPServerConnection* _Connection = (HTTPServerConnection*)_Context;

	switch(_Connection->state)
	{
		case HTTPServerConnection_State_Init:
		{
			_Connection->startTime = _MonTime;
			_Connection->state = HTTPServerConnection_State_Reading;
			break;
		}
		case HTTPServerConnection_State_Reading:
		{
			if (_MonTime - _Connection->startTime >= HTTPSERVER_TIMEOUT_MS)
			{
				printf("Connection timed out during reading.\n");
				_Connection->state = HTTPServerConnection_State_Waiting;
			}

			TCPClient* tcpClient = &_Connection->tcpClient;
			int read = TCPClient_Read(tcpClient, (uint8_t*)(_Connection->readBuffer + _Connection->bytesRead), READBUFFER_SIZE - _Connection->bytesRead - 1);

			if(read > 0)
			{
				_Connection->bytesRead += read;
				_Connection->readBuffer[_Connection->bytesRead] = '\0';
			}

			char* ret = strstr(_Connection->readBuffer, "\r\n\r\n");
			if(ret != NULL)
			{
				_Connection->state = HTTPServerConnection_State_Parsing;
			}

			break;
		}
		case HTTPServerConnection_State_Parsing:
		{
			HTTPRequest* request = HTTPRequest_fromstring(_Connection->readBuffer);

			_Connection->url = strdup(request->URL);
			_Connection->method = strdup(RequestMethod_tostring(request->method));

			printf("%s\n%s\n\n", _Connection->method, _Connection->url);
			
			HTTPRequest_Dispose(&request);

			_Connection->state = HTTPServerConnection_State_Done;

			break;
		}
		case HTTPServerConnection_State_Timeout:
		{
			printf("Connection timed out\n");
			_Connection->state = HTTPServerConnection_State_Waiting;
			break;
		}
		case HTTPServerConnection_State_Done:
		{
			if(strcmp(_Connection->method, "GET") == 0)
			{
				_Connection->onRequest(_Connection->context);
			}
			// Ska den verkligen disposea här?
			_Connection->state = HTTPServerConnection_State_Waiting;
			break;
		}
		case HTTPServerConnection_State_Waiting:
		{
			// HTTPServerConnection_Dispose(_Connection);
			break;
		}
		case HTTPServerConnection_State_Failed:
		{
			printf("Reading failed\n");
			_Connection->state = HTTPServerConnection_State_Waiting;
			break;
		}
		default:
		{
			printf("Unsupported state\n");
			break;
		}
	}
	
	//printf("HTTPServerConnection_TaskWork\n");
}

void HTTPServerConnection_Dispose(HTTPServerConnection* _Connection)
{
	TCPClient_Dispose(&_Connection->tcpClient);
	smw_destroyTask(_Connection->task);
}

void HTTPServerConnection_DisposePtr(HTTPServerConnection** _ConnectionPtr)
{
	if(_ConnectionPtr == NULL || *(_ConnectionPtr) == NULL)
		return;

	HTTPServerConnection_Dispose(*(_ConnectionPtr));
	free(*(_ConnectionPtr));
	*(_ConnectionPtr) = NULL;
}

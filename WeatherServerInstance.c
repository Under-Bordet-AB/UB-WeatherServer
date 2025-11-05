#include "WeatherServerInstance.h"
#include <stdlib.h>

#include "HTTPParser.h"
#include "utils.h"

//-----------------Internal Functions-----------------

int WeatherServerInstance_OnRequest(void* _Context);

//----------------------------------------------------

int WeatherServerInstance_Initiate(WeatherServerInstance* _Instance, HTTPServerConnection* _Connection)
{
	_Instance->connection = _Connection;

	HTTPServerConnection_SetCallback(_Instance->connection, _Instance, WeatherServerInstance_OnRequest);

	return 0;
}

int WeatherServerInstance_InitiatePtr(HTTPServerConnection* _Connection, WeatherServerInstance** _InstancePtr)
{
	if(_InstancePtr == NULL)
		return -1;

	WeatherServerInstance* _Instance = (WeatherServerInstance*)malloc(sizeof(WeatherServerInstance));
	if(_Instance == NULL)
		return -2;

	int result = WeatherServerInstance_Initiate(_Instance, _Connection);
	if(result != 0)
	{
		free(_Instance);
		return result;
	}

	*(_InstancePtr) = _Instance;

	return 0;
}

int WeatherServerInstance_OnRequest(void* _Context)
{
	WeatherServerInstance* connected_client = (WeatherServerInstance*)_Context;
	printf("WeatherServerInstance_OnRequest\n");

	connected_client->state = WeatherServerInstance_State_Init;

	return 0;
}

void WeatherServerInstance_Work(WeatherServerInstance* _Server, uint64_t _MonTime)
{
	// Select function to run in the weather API
//	_Server->connection->readBuffer;
	//_Server->connection->method;

	// _Server.state = WORKING
	// State machine
	/* If /get
	 * If /weather
	 * 		do A
	 * 		get_weather("citname");
	 * else if /surprise
	 * 		do B
	 *  WRITE(****)
	 */
	switch (_Server->state)
	{
		case WeatherServerInstance_State_Waiting:
		{

			// Wait for something to happen
			break;
		}
		case WeatherServerInstance_State_Init:
		{
			// Initialize stuff
			_Server->state = WeatherServerInstance_State_Work;
			// Initialize weather API
			break;
		}
		case WeatherServerInstance_State_Work:
		{
			// char* method = _Server->connection->method;
			char* url = _Server->connection->url;
			// printf("%s\n", url);
			if (strcmp(url, "/surprise") == 0)
			{
				// printf("SURPRISE!\n");
				// HTTPResponse* response = HTTPResponse_new(OK, "SURPRISE!\n");

				// if (HTTPResponse_add_header(response, "Access-Control-Allow-Origin", "*") == 0)
				// {
				// 	printf("Successfully added header\n");
				// }

				// const char* response_c = HTTPResponse_tostring(response);
				// printf("%s\n", response_c);

				// int byteswritten = TCPClient_Write(&_Server->connection->tcpClient, (uint8_t*)response_c, strlen(response_c));
				
				_Server->state = WeatherServerInstance_State_Done;

				break;
			}
			
			printf("404\n");

			break;
		}
		case WeatherServerInstance_State_Done:
		{
			// Finish up
			_Server->state = WeatherServerInstance_State_Dispose;
			break;
		}
		case WeatherServerInstance_State_Dispose:
		{
			// Dispose
			break;
		}
		default:
		{
			printf("Unsupported state\n");
			break;
		}
	}

	//_Server->connection->state = DONE!;
}

void WeatherServerInstance_Dispose(WeatherServerInstance* _Instance)
{

}

void WeatherServerInstance_DisposePtr(WeatherServerInstance** _InstancePtr)
{
	if(_InstancePtr == NULL || *(_InstancePtr) == NULL)
		return;

	WeatherServerInstance_Dispose(*(_InstancePtr));
	free(*(_InstancePtr));
	*(_InstancePtr) = NULL;
}

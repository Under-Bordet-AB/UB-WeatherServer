#include "WeatherServerInstance.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "utils.h"
#include "HTTPParser.h"

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
	WeatherServerInstance* instance = (WeatherServerInstance*)_Context;
	if(instance == NULL || instance->connection == NULL)
	{
		return -1;
	}

	HTTPServerConnection* conn = instance->connection;
	const char* url = conn->url;
	const char* method = conn->method;

	if(!url || !method)
	{
		// Bad request
		const char* errBody = "{\"error\":{\"code\":400,\"message\":\"Bad request\"}}";
		HTTPResponse* resp = HTTPResponse_new(Bad_Request, errBody);
		HTTPResponse_add_header(resp, "Content-Type", "application/json");
		char lenbuf[32];
		snprintf(lenbuf, sizeof(lenbuf), "%zu", strlen(errBody));
		HTTPResponse_add_header(resp, "Content-Length", lenbuf);
		const char* out = HTTPResponse_tostring(resp);
		TCPClient_Write(&conn->tcpClient, (const uint8_t*)out, strlen(out));
		free((void*)out);
		HTTPResponse_Dispose(&resp);
		return -1;
	}

	// Only GET supported for now
	if(strcmp(method, "GET") != 0)
	{
		const char* errBody = "{\"error\":{\"code\":405,\"message\":\"Method not allowed\"}}";
		HTTPResponse* resp = HTTPResponse_new(Method_Not_Allowed, errBody);
		HTTPResponse_add_header(resp, "Content-Type", "application/json");
		char lenbuf[32];
		snprintf(lenbuf, sizeof(lenbuf), "%zu", strlen(errBody));
		HTTPResponse_add_header(resp, "Content-Length", lenbuf);
		const char* out = HTTPResponse_tostring(resp);
		TCPClient_Write(&conn->tcpClient, (const uint8_t*)out, strlen(out));
		free((void*)out);
		HTTPResponse_Dispose(&resp);
		return 0;
	}

	// Route handling
	if(strcmp(url, "/cities") == 0)
	{
		const char* body = "{\"cities\":[{ \"name\": \"Stockholm\", \"latitude\": 59.3293, \"longitude\": 18.0686 },{ \"name\": \"Göteborg\", \"latitude\": 57.7089, \"longitude\": 11.9746 },{ \"name\": \"Malmö\", \"latitude\": 55.6050, \"longitude\": 13.0038 }]}";
		HTTPResponse* resp = HTTPResponse_new(OK, body);
		HTTPResponse_add_header(resp, "Content-Type", "application/json");
		char lenbuf[32];
		snprintf(lenbuf, sizeof(lenbuf), "%zu", strlen(body));
		HTTPResponse_add_header(resp, "Content-Length", lenbuf);
		const char* out = HTTPResponse_tostring(resp);
		TCPClient_Write(&conn->tcpClient, (const uint8_t*)out, strlen(out));
		free((void*)out);
		HTTPResponse_Dispose(&resp);
		return 0;
	}

	const char* weather_prefix = "/weather/";
	size_t pref_len = strlen(weather_prefix);
	if(strncmp(url, weather_prefix, pref_len) == 0)
	{
		const char* location = url + pref_len; // may be empty
		if(location == NULL || *location == '\0')
		{
			const char* errBody = "{\"error\":{\"code\":400,\"message\":\"Bad request: missing or invalid location parameter\"}}";
			HTTPResponse* resp = HTTPResponse_new(Bad_Request, errBody);
			HTTPResponse_add_header(resp, "Content-Type", "application/json");
			char lenbuf[32];
			snprintf(lenbuf, sizeof(lenbuf), "%zu", strlen(errBody));
			HTTPResponse_add_header(resp, "Content-Length", lenbuf);
			const char* out = HTTPResponse_tostring(resp);
			TCPClient_Write(&conn->tcpClient, (const uint8_t*)out, strlen(out));
			free((void*)out);
			HTTPResponse_Dispose(&resp);
			return 0;
		}

		// Simple hard-coded data for demo cities
		if(strcmp(location, "Stockholm") == 0)
		{
			const char* body = "{\"latitude\": \"59.3293\", \"longitude\": \"18.0686\", \"location\": \"Stockholm\", \"temperature\": 15.2, \"unit\": \"celsius\", \"condition\": \"Partly Cloudy\", \"humidity\": 70, \"wind_speed\": 10.5, \"wind_direction\": \"NW\"}";
			HTTPResponse* resp = HTTPResponse_new(OK, body);
			HTTPResponse_add_header(resp, "Content-Type", "application/json");
			char lenbuf[32];
			snprintf(lenbuf, sizeof(lenbuf), "%zu", strlen(body));
			HTTPResponse_add_header(resp, "Content-Length", lenbuf);
			const char* out = HTTPResponse_tostring(resp);
			TCPClient_Write(&conn->tcpClient, (const uint8_t*)out, strlen(out));
			free((void*)out);
			HTTPResponse_Dispose(&resp);
			return 0;
		}
		else if(strcmp(location, "Göteborg") == 0 || strcmp(location, "Goteborg") == 0 || strcmp(location, "Gothenburg") == 0)
		{
			const char* body = "{\"latitude\": \"57.7089\", \"longitude\": \"11.9746\", \"location\": \"Göteborg\", \"temperature\": 13.1, \"unit\": \"celsius\", \"condition\": \"Cloudy\", \"humidity\": 80, \"wind_speed\": 5.2, \"wind_direction\": \"W\"}";
			HTTPResponse* resp = HTTPResponse_new(OK, body);
			HTTPResponse_add_header(resp, "Content-Type", "application/json");
			char lenbuf[32];
			snprintf(lenbuf, sizeof(lenbuf), "%zu", strlen(body));
			HTTPResponse_add_header(resp, "Content-Length", lenbuf);
			const char* out = HTTPResponse_tostring(resp);
			TCPClient_Write(&conn->tcpClient, (const uint8_t*)out, strlen(out));
			free((void*)out);
			HTTPResponse_Dispose(&resp);
			return 0;
		}
		else if(strcmp(location, "Malmö") == 0 || strcmp(location, "Malmo") == 0)
		{
			const char* body = "{\"latitude\": \"55.6050\", \"longitude\": \"13.0038\", \"location\": \"Malmö\", \"temperature\": 16.0, \"unit\": \"celsius\", \"condition\": \"Sunny\", \"humidity\": 60, \"wind_speed\": 7.0, \"wind_direction\": \"NE\"}";
			HTTPResponse* resp = HTTPResponse_new(OK, body);
			HTTPResponse_add_header(resp, "Content-Type", "application/json");
			char lenbuf[32];
			snprintf(lenbuf, sizeof(lenbuf), "%zu", strlen(body));
			HTTPResponse_add_header(resp, "Content-Length", lenbuf);
			const char* out = HTTPResponse_tostring(resp);
			TCPClient_Write(&conn->tcpClient, (const uint8_t*)out, strlen(out));
			free((void*)out);
			HTTPResponse_Dispose(&resp);
			return 0;
		}

		// Not found
		const char* notFound = "{\"error\":{\"code\":404,\"message\":\"Location not found\"}}";
		HTTPResponse* resp = HTTPResponse_new(Not_Found, notFound);
		HTTPResponse_add_header(resp, "Content-Type", "application/json");
		char lenbuf[32];
		snprintf(lenbuf, sizeof(lenbuf), "%zu", strlen(notFound));
		HTTPResponse_add_header(resp, "Content-Length", lenbuf);
		const char* out = HTTPResponse_tostring(resp);
		TCPClient_Write(&conn->tcpClient, (const uint8_t*)out, strlen(out));
		free((void*)out);
		HTTPResponse_Dispose(&resp);
		return 0;
	}

	if(strcmp(url, "/surprise") == 0)
	{
		const char* body = "\"?\""; // JSON string containing ?
		HTTPResponse* resp = HTTPResponse_new(OK, body);
		HTTPResponse_add_header(resp, "Content-Type", "application/json");
		char lenbuf[32];
		snprintf(lenbuf, sizeof(lenbuf), "%zu", strlen(body));
		HTTPResponse_add_header(resp, "Content-Length", lenbuf);
		const char* out = HTTPResponse_tostring(resp);
		TCPClient_Write(&conn->tcpClient, (const uint8_t*)out, strlen(out));
		free((void*)out);
		HTTPResponse_Dispose(&resp);
		return 0;
	}

	// Default: 404
	const char* notFound = "{\"error\":{\"code\":404,\"message\":\"Not found\"}}";
	HTTPResponse* resp = HTTPResponse_new(Not_Found, notFound);
	HTTPResponse_add_header(resp, "Content-Type", "application/json");
	char lenbuf[32];
	snprintf(lenbuf, sizeof(lenbuf), "%zu", strlen(notFound));
	HTTPResponse_add_header(resp, "Content-Length", lenbuf);
	const char* out = HTTPResponse_tostring(resp);
	TCPClient_Write(&conn->tcpClient, (const uint8_t*)out, strlen(out));
	free((void*)out);
	HTTPResponse_Dispose(&resp);

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
			// Do the work
			// Process request
				// If /getweather
					// Call weather API
				// Else if /surprise
					// Do something else
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

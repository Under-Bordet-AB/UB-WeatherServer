#include "http_client.h"

void HTTPClient_Work(void* _Context, uint64_t _MonTime)
{
    HTTPClient* http_client = (HTTPClient*)(_Context);

	switch (http_client->http_client_state) {
        case HTTPClient_State_Init: {
            printf("HTTPClient_State_Init\n");
            if (TCPClient_Initiate(&http_client->tcp_client, -1) == 0) {
                http_client->http_client_state = HTTPClient_State_Connect;
            }
            break;
        }
        case HTTPClient_State_Connect: {
            printf("HTTPClient_State_Connect\n");
            if (TCPClient_Connect(&http_client->tcp_client, http_client->host, http_client->port) == 0) {
                http_client->http_client_state = HTTPClient_State_Transmit;
            }
            break;
        }
        case HTTPClient_State_Transmit: {
            printf("HTTPClient_State_Transmit\n");
            int byteswritten = TCPClient_Write(&http_client->tcp_client, http_client->bufferPtr, http_client->message_len);
            if (byteswritten > 0 ) {
                http_client->message_len -= byteswritten;
                http_client->bufferPtr += byteswritten;
            }
            
            if (http_client->message_len <= 0) {
                http_client->bufferPtr = http_client->buffer;
                http_client->message_len = 0;
                http_client->http_client_state = HTTPClient_State_Receive;
            }

            break;
        }
        case HTTPClient_State_Receive: {
            int space_remaining = 4096 - http_client->message_len - 1;
            if (space_remaining <= 0) {
                http_client->callback(http_client, "response_received");
                http_client->http_client_state = HTTPClient_State_Close;
                break;
            }

            int bytesread = TCPClient_Read(&http_client->tcp_client, http_client->bufferPtr, space_remaining);

            if (bytesread > 0) {
                http_client->message_len += bytesread;
                http_client->bufferPtr += bytesread;
            } else if (bytesread == 0 || http_client->tcp_client.fd < 0) {
                http_client->buffer[http_client->message_len] = '\0';
                http_client->callback(http_client, "response_received");
                http_client->http_client_state = HTTPClient_State_Close;
            }

            break;
        }
        case HTTPClient_State_Close: {
            printf("HTTPClient_State_Close\n");
            TCPClient_Disconnect(&http_client->tcp_client);
            TCPClient_Dispose(&http_client->tcp_client);
            http_client->working = 1;
            break;
        }
        default:
            break;
    }
}

int HTTPClient_Initiate(HTTPClient* _Client)
{
	memset(_Client, 0, sizeof(HTTPClient));
	
	_Client->buffer = NULL;
    _Client->http_client_state = HTTPClient_State_Init;

    _Client->working = 1;

	_Client->task = NULL;

	return 0;
}

int HTTPClient_GET(HTTPClient* _Client, const char* _URL, void (*callback)(HTTPClient* _CLient, const char* _Event))
{
	// Parse URL: http://host:port/path or http://host/path
	const char* host_start = strstr(_URL, "://");
	if (host_start == NULL) {
		host_start = _URL; // No protocol, assume it's just host/path
	} else {
		host_start += 3; // Skip "://"
	}

	// Find end of host (either ':', '/', or end of string)
	const char* host_end = strpbrk(host_start, ":/ ");
	if (host_end == NULL) {
		host_end = host_start + strlen(host_start);
	}

	// Extract host
	size_t host_len = host_end - host_start;
	_Client->host = malloc(host_len + 1);
	if (_Client->host == NULL)
		return -1;
	strncpy(_Client->host, host_start, host_len);
	_Client->host[host_len] = '\0';

	// Extract port (if present)
	const char* path_start;
	if (*host_end == ':') {
		// Port is specified
		const char* port_start = host_end + 1;
		const char* port_end = strchr(port_start, '/');
		if (port_end == NULL) {
			port_end = port_start + strlen(port_start);
		}
		size_t port_len = port_end - port_start;
		_Client->port = malloc(port_len + 1);
		if (_Client->port == NULL) {
			free(_Client->host);
			return -1;
		}
		strncpy(_Client->port, port_start, port_len);
		_Client->port[port_len] = '\0';
		path_start = port_end;
	} else {
		// No port specified, use default
		_Client->port = malloc(3);
		if (_Client->port == NULL) {
			free(_Client->host);
			return -1;
		}
		strcpy(_Client->port, "80");
		path_start = host_end;
	}

	// Extract path (or use "/" if no path)
	const char* path = (*path_start == '/') ? path_start : "/";

	// Allocate buffer for HTTP request
	_Client->buffer = malloc(4096);
	if(_Client->buffer == NULL) {
		free(_Client->host);
		free(_Client->port);
		return -1;
	}

	// Build HTTP request
	_Client->message_len = snprintf((char*)_Client->buffer, 4096, 
		"GET %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n", 
		path, _Client->host);

	_Client->bufferPtr = _Client->buffer;

	printf("HTTPClient connecting to %s:%s\n", _Client->host, _Client->port);
	printf("HTTPClient request:\n%s\n", _Client->buffer);

	_Client->task = smw_createTask(_Client, HTTPClient_Work);
	_Client->callback = callback;

    _Client->working = 0;

	return 0;
}

void HTTPClient_Dispose(HTTPClient* _Client)
{
	if(_Client->task != NULL)
		smw_destroyTask(_Client->task);

	if(_Client->buffer != NULL)
		free(_Client->buffer);

	if(_Client->host != NULL)
		free(_Client->host);

	if(_Client->port != NULL)
		free(_Client->port);
}

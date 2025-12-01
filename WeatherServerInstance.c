#include "WeatherServerInstance.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "backends/cities/cities.h"
#include "backends/geolocation/geolocation.h"
#include "backends/surprise/surprise.h"
#include "backends/weather/weather.h"

//-----------------Internal Functions-----------------

int WeatherServerInstance_OnRequest(void* _Context);
void WeatherServerInstance_OnDone(void* _Context);

//----------------------------------------------------

int WeatherServerInstance_Initiate(WeatherServerInstance* _Instance, HTTPServerConnection* _Connection) {
    _Instance->connection = _Connection;
    _Instance->state = WeatherServerInstance_State_Waiting;
    // Initialize backend to safe defaults
    _Instance->backend.backend_struct = NULL;
    _Instance->backend.backend_get_buffer = NULL;
    _Instance->backend.backend_get_buffer_size = NULL;
    _Instance->backend.backend_work = NULL;
    _Instance->backend.backend_dispose = NULL;
    _Instance->backend.binary_mode = 0;

    HTTPServerConnection_SetCallback(_Instance->connection, _Instance, WeatherServerInstance_OnRequest);

    return 0;
}

int WeatherServerInstance_InitiatePtr(HTTPServerConnection* _Connection, WeatherServerInstance** _InstancePtr) {
    if (_InstancePtr == NULL) return -1;

    WeatherServerInstance* _Instance = (WeatherServerInstance*)malloc(sizeof(WeatherServerInstance));
    if (_Instance == NULL) return -2;

    int result = WeatherServerInstance_Initiate(_Instance, _Connection);
    if (result != 0) {
        free(_Instance);
        return result;
    }

    *(_InstancePtr) = _Instance;

    return 0;
}

int WeatherServerInstance_OnRequest(void* _Context) {
    WeatherServerInstance* server = (WeatherServerInstance*)_Context;

    server->state = WeatherServerInstance_State_Init;
    return 0;
}

void WeatherServerInstance_OnDone(void* _Context) {
    WeatherServerInstance* server = (WeatherServerInstance*)_Context;

    server->state = WeatherServerInstance_State_Done;
}

void WeatherServerInstance_Work(WeatherServerInstance* _Server, uint64_t _MonTime) {
    // Don't access connection if we're disposing or already disposed
    if (_Server->state == WeatherServerInstance_State_Dispose) {
        WeatherServerBackend* backend = &_Server->backend;
        if (backend->backend_struct != NULL && backend->backend_dispose != NULL) {
            backend->backend_dispose(&backend->backend_struct);
        }
        _Server->state = WeatherServerInstance_State_This_Is_Actually_The_State_Where_We_Want_This_Struct_To_Be_Disposed;
        return;
    }
    
    if (_Server->connection == NULL) { return; }
    // If the connection task is gone, the connection is disposing - don't use it
    if (_Server->connection->task == NULL) { 
        _Server->state = WeatherServerInstance_State_Dispose;
        return;
    }
    if (_Server->connection->url == NULL) { return; }

    HTTPQuery* query = HTTPQuery_fromstring(_Server->connection->url);
    if (!query || !query->Path) {
        if (_Server->connection) { HTTPServerConnection_SendResponse(_Server->connection, 400, "Bad Request: malformed URL\n", "text/plain"); }
        if (query) HTTPQuery_Dispose(&query);
        _Server->state = WeatherServerInstance_State_Sending;
        return;
    }

    WeatherServerBackend* backend = &_Server->backend;

    switch (_Server->state) {
    case WeatherServerInstance_State_Waiting: {
        break;
    }
    case WeatherServerInstance_State_Init: {
        if (strcmp(query->Path, "/GetCities") == 0) {
            cities_init((void*)_Server, &backend->backend_struct, WeatherServerInstance_OnDone);
            backend->backend_get_buffer = cities_get_buffer;
            backend->backend_work = cities_work;
            backend->backend_dispose = cities_dispose;
            backend->binary_mode = 0;

        } else if (strcmp(query->Path, "/GetLocation") == 0) {
            geolocation_init((void*)_Server, &backend->backend_struct, WeatherServerInstance_OnDone);
            backend->backend_get_buffer = geolocation_get_buffer;
            backend->backend_work = geolocation_work;
            backend->backend_dispose = geolocation_dispose;
            backend->binary_mode = 0;

            char* location_name = (char*)HTTPQuery_getParameter(query, "name");
            char* location_count_string = (char*)HTTPQuery_getParameter(query, "count");
            char* country_code = (char*)HTTPQuery_getParameter(query, "countryCode");
            
            if (location_name == NULL) {
                HTTPServerConnection_SendResponse(_Server->connection, 400, "Bad Request: Missing 'name' parameter\n", "text/plain");
                _Server->state = WeatherServerInstance_State_Sending;
                break;
            }
            
            int location_count = location_count_string ? (int)strtol(location_count_string, NULL, 10) : 5;

            geolocation_set_parameters(&backend->backend_struct, location_name, location_count, country_code);

        }  else if (strcmp(query->Path, "/GetWeather") == 0) {
            weather_init((void*)_Server, &backend->backend_struct, WeatherServerInstance_OnDone);
            backend->backend_get_buffer = weather_get_buffer;
            backend->backend_work = weather_work;
            backend->backend_dispose = weather_dispose;
            backend->binary_mode = 0;

            const char* lat_str = HTTPQuery_getParameter(query, "lat");
            const char* lon_str = HTTPQuery_getParameter(query, "lon");
            if (lat_str == NULL || lon_str == NULL) {
                HTTPServerConnection_SendResponse(_Server->connection, 400, "Bad Request: Missing parameters\n", "text/plain");
                _Server->state = WeatherServerInstance_State_Sending;
                break;
            }

            double latitude = round(strtod(lat_str, NULL) * 100.0) / 100.0;
            double longitude = round(strtod(lon_str, NULL) * 100.0) / 100.0;
            weather_set_location(&backend->backend_struct, latitude, longitude);

        } else if (strcmp(query->Path, "/GetSurprise") == 0) {
            surprise_init((void*)_Server, &backend->backend_struct, WeatherServerInstance_OnDone);
            backend->backend_get_buffer = surprise_get_buffer;
            backend->backend_get_buffer_size = surprise_get_buffer_size;
            backend->backend_work = surprise_work;
            backend->backend_dispose = surprise_dispose;
            backend->binary_mode = 1;
            
        } else {
            HTTPServerConnection_SendResponse(_Server->connection, 404, "Not Found\n", "text/plain");
            _Server->state = WeatherServerInstance_State_Sending;
            break;
        }
        _Server->state = WeatherServerInstance_State_Work;
        break;
    }
    case WeatherServerInstance_State_Work: {
        printf("WeatherServerInstance: Working...\n");
        backend->backend_work(&backend->backend_struct);
        break;
    }
    case WeatherServerInstance_State_Done: {
        char* buffer;
        backend->backend_get_buffer(&backend->backend_struct, &buffer);

        if (backend->binary_mode == 1) {
            if (buffer == NULL) {
                HTTPServerConnection_SendResponse(_Server->connection, 500, "Internal Server Error\n", "text/plain");
                _Server->state = WeatherServerInstance_State_Sending;
                break;
            }
            size_t buffer_size;
            backend->backend_get_buffer_size(&backend->backend_struct, &buffer_size);
            HTTPServerConnection_SendResponse_Binary(_Server->connection, 200, (uint8_t*)buffer, buffer_size, "image/png");
            _Server->state = WeatherServerInstance_State_Sending;
            printf("WeatherServerInstance: Done.\n");
            break;
        } else {
            if (buffer == NULL) {
                HTTPServerConnection_SendResponse(_Server->connection, 500, "Internal Server Error\n", "text/plain");
                _Server->state = WeatherServerInstance_State_Sending;
                break;
            }

            HTTPServerConnection_SendResponse(_Server->connection, 200, buffer, "application/json");
            _Server->state = WeatherServerInstance_State_Sending;
            printf("WeatherServerInstance: Done.\n");
            break;
        }

        break;
    }
    case WeatherServerInstance_State_Sending:
        if (_Server->connection->state == HTTPServerConnection_State_Dispose)
        {
            _Server->state = WeatherServerInstance_State_Dispose;
            break;
        }
        break;
    case WeatherServerInstance_State_This_Is_Actually_The_State_Where_We_Want_This_Struct_To_Be_Disposed:

        break;
    default: {
        break;
    }
    }

    if (query) HTTPQuery_Dispose(&query);
}

void WeatherServerInstance_Dispose(WeatherServerInstance* _Instance) {
    HTTPServerConnection_Dispose(_Instance->connection);
    free(_Instance->connection);
    free(_Instance);
}

void WeatherServerInstance_DisposePtr(WeatherServerInstance** _InstancePtr) {
    if (_InstancePtr == NULL || *(_InstancePtr) == NULL) return;

    WeatherServerInstance_Dispose(*(_InstancePtr));
    free(*(_InstancePtr));
    *(_InstancePtr) = NULL;
}
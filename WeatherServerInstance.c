#include "WeatherServerInstance.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cities/cities_new.h"

//-----------------Internal Functions-----------------

int WeatherServerInstance_OnRequest(void* _Context);
int WeatherServerInstance_OnDone(void* _Context);

//----------------------------------------------------

int WeatherServerInstance_Initiate(WeatherServerInstance* _Instance, HTTPServerConnection* _Connection) {
    _Instance->connection = _Connection;

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

int WeatherServerInstance_OnDone(void* _Context) {
    WeatherServerInstance* server = (WeatherServerInstance*)_Context;

    server->state = WeatherServerInstance_State_Done;
    return 0;
}

void WeatherServerInstance_Work(WeatherServerInstance* _Server, uint64_t _MonTime) {
    switch (_Server->state) {
    case WeatherServerInstance_State_Init: {
        char* url = _Server->connection->url;
        if (strcmp(url, "/GetCities") == 0) {
            // cities_init(weatherbackend_struct);
            // weatherbackend_work = cities_work(weatherbackend_struct);
            // weatherbackend_buffer = cities_buffer(weatherbackend_struct);
            // weatherbackend_dispose = cities_dispose(weatherbackend_struct);
        } else if (strcmp(url, "/GetWeather") == 0) {
            // weather_init(weatherbackend_struct);
            // weatherbackend_work = weather_work(weatherbackend_struct);
            // weatherbackend_buffer = weather_buffer(weatherbackend_struct);
            // weatherbackend_dispose = weather_dispose(weatherbackend_struct);
        } else if (strcmp(url, "/GetSurprise") == 0) {
            // surprise_init(weatherbackend_struct);
            // weatherbackend_work = surprise_work(weatherbackend_struct);
            // weatherbackend_buffer = surprise_buffer(weatherbackend_struct);
            // weatherbackend_dispose = surprise_dispose(weatherbackend_struct);
        }
        _Server->state = WeatherServerInstance_State_Work;
        break;
    }
    case WeatherServerInstance_State_Work: {
        // _Server->backend_work(_Server->backend_struct);
        break;
    }
    case WeatherServerInstance_State_Done: {
        // SendResponse(buffer);
        //_Server->state = WeatherServerInstance_State_Dispose;
        break;
    }
    case WeatherServerInstance_State_Dispose: {
        //_Server->backend_dispose(_Server->backend_struct);
        break;
    }
    default: {
        break;
    }
    }

    _Server->state = WeatherServerInstance_State_Done;
}

void WeatherServerInstance_Dispose(WeatherServerInstance* _Instance) {
}

void WeatherServerInstance_DisposePtr(WeatherServerInstance** _InstancePtr) {
    if (_InstancePtr == NULL || *(_InstancePtr) == NULL) return;

    WeatherServerInstance_Dispose(*(_InstancePtr));
    free(*(_InstancePtr));
    *(_InstancePtr) = NULL;
}

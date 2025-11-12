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
    WeatherServerBackend* backend = &_Server->backend;
    char* url = _Server->connection->url;
    void** backend_t = backend->backend_struct;

    switch (_Server->state) {
    case WeatherServerInstance_State_Init: {
        if (strcmp(url, "/GetCities") == 0) {
            cities_init(backend_t, WeatherServerInstance_OnDone);
            backend->answer_get_buffer = cities_get_buffer;
            backend->answer_work = cities_work;
            backend->answer_dispose = cities_dispose;
        } else if (strcmp(url, "/GetWeather") == 0) {
            // weather_init(backend_t, WeatherServerInstance_OnDone);
            // backend->answer_get_buffer = weather_get_buffer;
            // backend->answer_work = weather_work;
            // backend->answer_dispose = weather_dispose;
        } else if (strcmp(url, "/GetSurprise") == 0) {
            // surprise_init(backend_t, WeatherServerInstance_OnDone);
            // backend->answer_get_buffer = surprise_get_buffer;
            // backend->answer_work = surprise_work;
            // backend->answer_dispose = surprise_dispose;
        }
        _Server->state = WeatherServerInstance_State_Work;
        break;
    }
    case WeatherServerInstance_State_Work: {
        backend->answer_work(backend_t);
        break;
    }
    case WeatherServerInstance_State_Done: {
        char* buffer;
        backend->answer_get_buffer(backend_t, &buffer);
        // SendResponse(buffer);
        _Server->state = WeatherServerInstance_State_Dispose;
        break;
    }
    case WeatherServerInstance_State_Dispose: {
        backend->answer_dispose(backend_t);
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

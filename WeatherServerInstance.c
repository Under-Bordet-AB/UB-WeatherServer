#include "WeatherServerInstance.h"
#include "libs/HTTPServer/HTTPServerConnection.h"
#include "libs/smw.h"
#include <stdlib.h>

//-----------------Internal Functions-----------------

int WeatherServerInstance_OnRequest(void *_Context);
void WeatherServerInstance_TaskWork(void *_Context, uint64_t _MonTime);

//----------------------------------------------------

int WeatherServerInstance_Initiate(WeatherServerInstance *_Instance,
                                   HTTPServerConnection *_Connection) {
  _Instance->connection = _Connection;
  // Now adds itself to smw-slavework list
  _Instance->task       = smw_createTask(_Instance, WeatherServerInstance_TaskWork);

  HTTPServerConnection_SetCallback(_Instance->connection, _Instance,
                                   WeatherServerInstance_OnRequest);

  return 0;
}

int WeatherServerInstance_InitiatePtr(HTTPServerConnection *_Connection,
                                      WeatherServerInstance **_InstancePtr) {
  if (_InstancePtr == NULL)
    return -1;

  WeatherServerInstance *_Instance =
      (WeatherServerInstance *)malloc(sizeof(WeatherServerInstance));
  if (_Instance == NULL)
    return -2;

  int result = WeatherServerInstance_Initiate(_Instance, _Connection);
  if (result != 0) {
    free(_Instance);
    return result;
  }

  *(_InstancePtr) = _Instance;

  return 0;
}

int WeatherServerInstance_OnRequest(void *_Context) {
  WeatherServerInstance *server = (WeatherServerInstance *)_Context;

  server->state = WeatherServerInstance_State_Work;
  return 0;
}

// REDONE TO MATCH FUNCTION CALL SIGNATURE
void WeatherServerInstance_TaskWork(void *_Context, uint64_t _MonTime) 
{
  WeatherServerInstance *_Instance = (WeatherServerInstance*)_Context;

  switch (_Instance->state) {
  case WeatherServerInstance_State_Waiting: {
    // Wait for something to happen
    break;
  }
  case WeatherServerInstance_State_Init: {
    // Initialize stuff
    _Instance->state = WeatherServerInstance_State_Work;
    // Initialize weather API
    break;
  }
  case WeatherServerInstance_State_Work: {
    if (strcmp(_Instance->connection->url, "/health") == 0) {
        // FOR CHECKING
        HTTPServerConnection_SendResponse(_Instance->connection, 200, "{JSON:SHIT}");
    } else if (strcmp(_Instance->connection->url, "/GetWeather") == 0) {
      // TODO:
      // void getweather(void context*, const char* cityname);
    }

    _Instance->state = WeatherServerInstance_State_Done;
    break;
  }
  case WeatherServerInstance_State_Chilling: {
    // Wait for data from backend to be ready for sending to client.
    break;
  }
  case WeatherServerInstance_State_Done: {
    // Finish up
    _Instance->state = WeatherServerInstance_State_Dispose;
    break;
  }
  case WeatherServerInstance_State_Dispose: {
    // Dispose
    break;
  }
  default: {
    printf("Unsupported state\n");
    break;
  }
  }

  _Instance->state = WeatherServerInstance_State_Done;
}

void WeatherServerInstance_Dispose(WeatherServerInstance *_Instance) {}

void WeatherServerInstance_DisposePtr(WeatherServerInstance **_InstancePtr) {
  if (_InstancePtr == NULL || *(_InstancePtr) == NULL)
    return;

  WeatherServerInstance_Dispose(*(_InstancePtr));
  free(*(_InstancePtr));
  *(_InstancePtr) = NULL;
}

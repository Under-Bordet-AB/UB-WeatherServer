#include "WeatherServerInstance.h"
#include <stdlib.h>

//-----------------Internal Functions-----------------

int WeatherServerInstance_OnRequest(void *_Context);

//----------------------------------------------------

int WeatherServerInstance_Initiate(WeatherServerInstance *_Instance,
                                   HTTPServerConnection *_Connection) {
  _Instance->connection = _Connection;

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

void WeatherServerInstance_Work(WeatherServerInstance *_Server,
                                uint64_t _MonTime) {
  // Select function to run in the weather API
  //_Server->connection->readBuffer;
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
  switch (_Server->state) {
  case WeatherServerInstance_State_Waiting: {
    // Wait for something to happen
    break;
  }
  case WeatherServerInstance_State_Init: {
    // Initialize stuff
    _Server->state = WeatherServerInstance_State_Work;
    // Initialize weather API
    break;
  }
  case WeatherServerInstance_State_Work: {
    // Do the work
    // Process request
    // If /getweather
    // Call weather API
    // Else if /surprise
    // Do something else
    if (strcmp(_Server->connection->url, "/health") == 0) {
      HTTPServerConnection_SendResponse(_Server->connection, 200, "");
    } else if (strcmp(_Server->connection->url, "/secret") == 0) {
      HTTPServerConnection_SendResponse(_Server->connection, 501, "");
    } else {
      HTTPServerConnection_SendResponse(_Server->connection, 404, "");
    }
    _Server->state = WeatherServerInstance_State_Done;
    break;
  }
  case WeatherServerInstance_State_Done: {
    // Finish up
    _Server->state = WeatherServerInstance_State_Dispose;
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

  _Server->state = WeatherServerInstance_State_Done;
}

void WeatherServerInstance_Dispose(WeatherServerInstance *_Instance) {}

void WeatherServerInstance_DisposePtr(WeatherServerInstance **_InstancePtr) {
  if (_InstancePtr == NULL || *(_InstancePtr) == NULL)
    return;

  WeatherServerInstance_Dispose(*(_InstancePtr));
  free(*(_InstancePtr));
  *(_InstancePtr) = NULL;
}

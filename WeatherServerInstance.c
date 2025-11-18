#include "WeatherServerInstance.h"
#include "surprise.h"
#include <stdlib.h>
#include "utils.h"

//-----------------Internal Functions-----------------

int WeatherServerInstance_OnRequest(void *_Context);

//----------------------------------------------------

int WeatherServerInstance_Initiate(WeatherServerInstance *_Instance,
                                   HTTPServerConnection *_Connection) {
  _Instance->connection = _Connection;
  _Instance->state = WeatherServerInstance_State_Init;

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

    HTTPQuery* query = HTTPQuery_fromstring(_Server->connection->url);
    printf("WeatherServerInstance received URL request: %s\n", query->Path);
    if(query->Query->size > 0)
    {
      printf("Parameters (%zu):",query->Query->size);
      LinkedList_foreach(query->Query, node) {
        HTTPQueryParameter* param = (HTTPQueryParameter*)node->item;
        printf("\n%s: %s",param->Name,param->Value);
      }
      printf("\n\n");
    }

    if (strcmp(query->Path, "/health") == 0) {
      HTTPServerConnection_SendResponse(_Server->connection, 200, "{\"status\":\"ok\"}", "application/json");
    } else if (strcmp(_Server->connection->url, "/GetCities") == 0) {
      // TODO:
      // void getcities(context);
    } else if (strcmp(_Server->connection->url, "/GetWeather") == 0) {
      // TODO:
      // void getweather(void context*, const char* cityname);
    }  else if (strcmp(query->Path, "/surprise") == 0) {
      uint8_t *buf;
      int buf_len = surprise_get_file(&buf, "surprise.png");
      if (buf_len < 0){
        HTTPServerConnection_SendResponse(_Server->connection, 404, "", "text/plain");
      } else {
        HTTPServerConnection_SendResponse_Binary(_Server->connection, 200, buf, buf_len, "application/octet-stream");
        free(buf);
      }
    } else {
      HTTPServerConnection_SendResponse(_Server->connection, 404, "Not Found", "text/plain");
    }

    _Server->state = WeatherServerInstance_State_Done;

    HTTPQuery_Dispose(&query);

    break;
  }
  case WeatherServerInstance_State_Chilling: {
    // Wait for data from backend to be ready for sending to client.
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

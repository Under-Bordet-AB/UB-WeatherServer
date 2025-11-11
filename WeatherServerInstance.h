
#ifndef __WeatherServerInstance_h_
#define __WeatherServerInstance_h_

#include "HTTPServer/HTTPServerConnection.h"
#include "libs/smw.h"
#include "smw.h"

typedef enum {
    WeatherServerInstance_State_Waiting,
    WeatherServerInstance_State_Init,
    WeatherServerInstance_State_Work,
    WeatherServerInstance_State_Chilling,
    WeatherServerInstance_State_Done,
    WeatherServerInstance_State_Dispose
} WeatherServerInstance_State;

typedef struct {
    HTTPServerConnection* connection;
    WeatherServerInstance_State state;
    smw_task *task;
} WeatherServerInstance;

int WeatherServerInstance_Initiate(WeatherServerInstance* _Instance, HTTPServerConnection* _Connection);
int WeatherServerInstance_InitiatePtr(HTTPServerConnection* _Connection, WeatherServerInstance** _InstancePtr);

void WeatherServerInstance_Work(WeatherServerInstance* _Instance, uint64_t _MonTime);

void WeatherServerInstance_Dispose(WeatherServerInstance* _Instance);
void WeatherServerInstance_DisposePtr(WeatherServerInstance** _InstancePtr);

#endif //__WeatherServerInstance_h_

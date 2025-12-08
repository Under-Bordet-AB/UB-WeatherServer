
#ifndef __WeatherServer_h_
#define __WeatherServer_h_

#include "HTTPServer/HTTPServer.h"
#include "linked_list.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/debug.h"
#include "mbedtls/entropy.h"
#include "mbedtls/net_sockets.h"
#include "mbedtls/ssl.h"
#include "smw.h"

#include "WeatherServerInstance.h"

typedef struct {
    HTTPServer httpServer;

    mbedtls_net_context net;           // TCP-socket
    mbedtls_ssl_context ssl;           // TLS-sessionens tillstånd
    mbedtls_ssl_config conf;           // TLS inställningar
    mbedtls_ctr_drbg_context ctr_drbg; // pseudo-slumptalsgenerator
    mbedtls_entropy_context entropy;   // entropikälla

    LinkedList* instances;

    smw_task* task;

} WeatherServer;

int WeatherServer_Initiate(WeatherServer* _Server);
int WeatherServer_InitiatePtr(WeatherServer** _ServerPtr);

void WeatherServer_Dispose(WeatherServer* _Server);
void WeatherServer_DisposePtr(WeatherServer** _ServerPtr);

#endif //__WeatherServer_h_

#include "WeatherServer.h"
#include <stdlib.h>

//-----------------Internal Functions-----------------

void WeatherServer_TaskWork(void* _Context, uint64_t _MonTime);
int WeatherServer_OnHTTPConnection(void* _Context, HTTPServerConnection* _Connection);

//----------------------------------------------------

int WeatherServer_Initiate(WeatherServer* _Server) {
    HTTPServer_Initiate(&_Server->httpServer, WeatherServer_OnHTTPConnection);

    // Dra igång embedtls
    mbedtls_net_init(&_Server->net);
    mbedtls_ssl_init(&_Server->ssl);
    mbedtls_ssl_config_init(&_Server->conf);
    mbedtls_ctr_drbg_init(&_Server->ctr_drbg);
    mbedtls_entropy_init(&_Server->entropy);
    const char* pers = "denna unika sträng läggs till seeden så vår seed blir helt unik.";

    int res_seed = mbedtls_ctr_drbg_seed(&_Server->ctr_drbg, mbedtls_entropy_func, &_Server->entropy, (const unsigned char*)pers, strlen(pers));
    if (res_seed != 0) {
        printf("WeatherServer_Initiate: mbedtls_ctr_drbg_seed failed with code %d\n", res_seed);
        return -1;
    }
    // Ändrad till MBEDTLS_SSL_IS_SERVER mot exemplet i 802 materialet som var för klient
    int res_config_defaults = mbedtls_ssl_config_defaults(&_Server->conf, MBEDTLS_SSL_IS_SERVER, MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT);
    if (res_config_defaults != 0) {
        printf("WeatherServer_Initiate: mbedtls_ssl_config_defaults failed with code %d\n", res_config_defaults);
        return -2;
    }

    // Attach RNG to the SSL config
    mbedtls_ssl_conf_rng(&_Server->conf, mbedtls_ctr_drbg_random, &_Server->ctr_drbg);

    // Här kan man även lägga in en debug callback i embedTLS om man vill

    _Server->instances = LinkedList_create();

    _Server->task = smw_createTask(_Server, WeatherServer_TaskWork);

    return 0;
}

int WeatherServer_InitiatePtr(WeatherServer** _ServerPtr) {
    if (_ServerPtr == NULL) return -1;

    WeatherServer* _Server = (WeatherServer*)malloc(sizeof(WeatherServer));
    if (_Server == NULL) return -2;

    int result = WeatherServer_Initiate(_Server);
    if (result != 0) {
        free(_Server);
        return result;
    }

    *(_ServerPtr) = _Server;

    return 0;
}

int WeatherServer_OnHTTPConnection(void* _Context, HTTPServerConnection* _Connection) {
    WeatherServer* _Server = (WeatherServer*)_Context;

    WeatherServerInstance* instance = NULL;
    int result = WeatherServerInstance_InitiatePtr(_Connection, &instance);
    if (result != 0) {
        printf("WeatherServer_OnHTTPConnection: Failed to initiate instance\n");
        return -1;
    }

    LinkedList_append(_Server->instances, instance);

    return 0;
}

void WeatherServer_TaskWork(void* _Context, uint64_t _MonTime) {
    WeatherServer* _Server = (WeatherServer*)_Context;

    LinkedList* done = LinkedList_create();
    // First pass: work on all instances
    LinkedList_foreach(_Server->instances, node) {
        WeatherServerInstance* instance = (WeatherServerInstance*)node->item;
        WeatherServerInstance_Work(instance, _MonTime);

        if (instance->state == WeatherServerInstance_State_This_Is_Actually_The_State_Where_We_Want_This_Struct_To_Be_Disposed) {
            LinkedList_append(done, node);
        }
    }

    LinkedList_foreach(done, node) {
        LinkedList_remove(_Server->instances, node->item, (void (*)(void*))WeatherServerInstance_Dispose);
    }

    LinkedList_dispose(&done, NULL);
}

void WeatherServer_Dispose(WeatherServer* _Server) {
    // Free mbedTLS resources
    mbedtls_ssl_free(&_Server->ssl);           // Free SSL context
    mbedtls_ssl_config_free(&_Server->conf);   // Free SSL configuration
    mbedtls_ctr_drbg_free(&_Server->ctr_drbg); // Free DRBG
    mbedtls_entropy_free(&_Server->entropy);   // Free entropy source
    mbedtls_net_free(&_Server->net);           // Free server socket

    // Free HTTP server and related resources
    HTTPServer_Dispose(&_Server->httpServer);
    smw_destroyTask(_Server->task);
    LinkedList_dispose(&_Server->instances, (void (*)(void*))WeatherServerInstance_Dispose);
}

void WeatherServer_DisposePtr(WeatherServer** _ServerPtr) {
    if (_ServerPtr == NULL || *(_ServerPtr) == NULL) return;

    WeatherServer_Dispose(*(_ServerPtr));
    free(*(_ServerPtr));
    *(_ServerPtr) = NULL;
}
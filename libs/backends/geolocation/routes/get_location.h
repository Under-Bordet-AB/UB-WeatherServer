#include "backends/geolocation/geolocation_model.h"
#include "utilities/curl_client.h"

typedef enum {
    bc_state_init,
    bc_state_search_for_candidates,
    bc_state_fetch_from_api_init,
    bc_state_fetch_from_api_request,
    bc_state_fetch_from_api_poll,
    bc_state_fetch_from_api_read,
    bc_state_process_response,
    bc_state_save_to_disk,
    bc_state_done
} get_location_state;

typedef struct get_location_t {
    void* ctx;
    void (*on_done)(void* ctx);

    curl_client_t* curl_client;

    char* location_name;
    int location_count;
    char* country_code;

    geolocation_t* locations;

    get_location_state state;

    char* buffer;
    int bytesread;
} get_location_t;

int get_location_init(void** ctx, void** ctx_struct, void (*on_done)(void* context));
int get_location_work(void** ctx);
int get_location_get_buffer(void** ctx, char** buffer);
int get_location_dispose(void** ctx);

int get_location_set_params(void** ctx, char* location_name, int location_count, char* country_code);
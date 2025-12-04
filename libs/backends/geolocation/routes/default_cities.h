#include "backends/geolocation/geolocation_model.h"

typedef enum {
    dc_state_init,
    dc_state_load_from_disk,
    dc_state_done
} default_cities_state;

typedef struct default_cities_t {
    void* ctx;
    void (*on_done)(void* ctx);

    geolocation_t locations[16];

    default_cities_state state;

    char* buffer;
    int bytesread;
} default_cities_t;

int default_cities_init(void** ctx, void** ctx_struct, void (*on_done)(void* context));
int default_cities_work(void** ctx);
int default_cities_get_buffer(void** ctx, char** buffer);
int default_cities_dispose(void** ctx);

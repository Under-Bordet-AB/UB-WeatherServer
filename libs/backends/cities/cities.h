#ifndef _CITIES_H
#define _CITIES_H

#include "linked_list.h"

typedef enum {
    Cities_State_Init,
    Cities_State_ReadFiles,
    Cities_State_ReadString,
    Cities_State_SaveToDisk,
    Cities_State_Convert,
    Cities_State_Done
} cities_state;

typedef struct cities_t {
    void* ctx;
    void (*on_done)(void* ctx);

    LinkedList* cities_list;
    cities_state state;
    char* buffer;
    int bytesread;
} cities_t;

typedef struct city_t {
    char* name;
    float latitude;
    float longitude;
} city_t;

int cities_init(void** ctx, void** ctx_struct, void (*ondone)(void* context));
int cities_get_buffer(void** ctx, char** buffer);
int cities_work(void** ctx);
int cities_dispose(void** ctx);

#endif

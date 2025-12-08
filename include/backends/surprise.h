#ifndef SURPRISE_H
#define SURPRISE_H

#include <stdint.h>
#include <stdlib.h>
#include "tinydir.h"

typedef enum {
    Surprise_State_Init,
    Surprise_State_Load_From_Disk,
    Surprise_State_Done
} surprise_state;

typedef struct surprise_t {
    void* ctx;
    void (*on_done)(void* ctx);

    surprise_state state;
    uint8_t* buffer;
    int bytesread;
} surprise_t;

int surprise_init(void** ctx, void** ctx_struct, void (*ondone)(void* context));
int surprise_get_buffer(void** ctx, char** buffer);
int surprise_get_buffer_size(void** ctx, size_t* size);
int surprise_work(void** ctx);
int surprise_dispose(void** ctx);

int surprise_get_file(uint8_t **buffer_ptr, const char *file_name);
int surprise_get_random(uint8_t **buffer_ptr);

#endif
#include "surprise.h"

#include "ui.h"
#include "w_client.h"
#include <stdio.h>
#include <stdlib.h>

#define IMAGE_NAME "bonzi.png"

size_t surprise_get_file(uint8_t** buffer_ptr) {
    // Open specified file i mode "rb" - read binary
    FILE* fptr = fopen("bonzi.png", "rb");
    if (!fptr)
        return -1;

    // find end of file and calculate file size
    fseek(fptr, 0, SEEK_END);
    long file_size_raw = ftell(fptr);
    if (file_size_raw < 0) {
        fclose(fptr);
        return -1; // ftell failed
    }
    size_t file_size = (size_t)file_size_raw;
    fseek(fptr, 0, SEEK_SET);

    uint8_t* buffer = (uint8_t*)malloc(sizeof(uint8_t) * file_size);
    if (!buffer) {
        // Failed to allocated memory
        fclose(fptr);
        return -2;
    }

    size_t bytes_read = fread(buffer, 1, file_size, fptr);
    if (bytes_read != file_size) {
        // Failed to read file
        free(buffer);
        fclose(fptr);
        return 0; // Return 0 on failure
    }

    fclose(fptr); // Close file on success
    *buffer_ptr = buffer;

    return file_size;
}

int surprise_init(void** ctx, void** ctx_struct, void (*ondone)(void* context)) {
    surprise_t* surprise = (surprise_t*)malloc(sizeof(surprise_t));
    if (!surprise) {
        return -1; // Memory allocation failed
    }
    surprise->ctx = ctx;
    surprise->state = Surprise_State_Init;
    surprise->buffer = NULL;
    surprise->bytesread = 0;
    surprise->on_done = ondone;
    *ctx_struct = (void*)surprise;

    w_client* client = (w_client*)ctx; // ctx is already void**, which points to w_client
    ui_print_backend_init(client, "Surprise");

    return 0;
}

int surprise_get_buffer(void** ctx, char** buffer) {
    surprise_t* surprise = (surprise_t*)(*ctx);
    if (!surprise) {
        return -1; // Memory allocation failed
    }
    *buffer = (char*)surprise->buffer;

    return 0;
}

int surprise_get_buffer_size(void** ctx, size_t* size) {
    surprise_t* surprise = (surprise_t*)(*ctx);
    if (!surprise) {
        return -1; // Memory allocation failed
    }
    *size = surprise->bytesread;

    return 0;
}

int surprise_work(void** ctx) {
    surprise_t* surprise = (surprise_t*)(*ctx);
    if (!surprise) {
        return -1; // Memory allocation failed
    }

    w_client* client = (w_client*)surprise->ctx; // surprise->ctx is void**, which points to w_client

    ui_print_backend_state(client, "Surprise", "loading image file");

    surprise->bytesread = surprise_get_file(&surprise->buffer);
    if (surprise->bytesread == 0) {
        // Failed to load file
        surprise->buffer = NULL;
        ui_print_backend_error(client, "Surprise", "failed to load image file");
    } else {
        ui_print_backend_state(client, "Surprise", "loaded image from disk");
    }

    ui_print_backend_done(client, "Surprise");
    surprise->on_done(surprise->ctx);

    return 0;
}

int surprise_dispose(void** ctx) {
    surprise_t* surprise = (surprise_t*)(*ctx);
    if (!surprise)
        return -1; // Memory allocation failed

    if (surprise->buffer) {
        free(surprise->buffer);
        surprise->buffer = NULL;
    }

    free(surprise);
    *ctx = NULL;

    return 0;
}

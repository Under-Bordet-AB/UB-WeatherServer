#include "surprise.h"

#include <stdio.h>
#include <stdlib.h>

#define IMAGE_NAME "surprise.png"

int surprise_get_file(uint8_t **buffer_ptr) {
  // Open specified file i mode "rb" - read binary
  FILE *fptr = fopen(IMAGE_NAME, "rb");
  if (!fptr)
    return -1;

  // find end of file and calculate file size
  fseek(fptr, 0, SEEK_END);
  long file_size = ftell(fptr);
  fseek(fptr, 0, SEEK_SET);

  uint8_t *buffer = (uint8_t *)malloc(sizeof(uint8_t) * file_size);
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
  }

  *buffer_ptr = buffer;

  return file_size;
}

int surprise_init(void** ctx, void** ctx_struct, void (*ondone)(void* context))
{
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

  printf("Surprise: Initialized struct\n");

  return 0;
}

int surprise_get_buffer(void** ctx, char** buffer)
{
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

int surprise_work(void** ctx)
{
  surprise_t* surprise = (surprise_t*)(*ctx);
    if (!surprise) {
        return -1; // Memory allocation failed
    }

    switch (surprise->state) {
    case Surprise_State_Init:
        surprise->state = Surprise_State_Load_From_Disk;
        printf("Surprise: Initialized\n");
        break;
    case Surprise_State_Load_From_Disk:
        surprise->bytesread = surprise_get_file(&surprise->buffer);
        surprise->state = Surprise_State_Done;
        printf("Surprise: Loaded from disk\n");
        break;
    case Surprise_State_Done:
        surprise->on_done(surprise->ctx);
        printf("Surprise: Done\n");
        break;
    }

    return 0;
}

int surprise_dispose(void** ctx)
{
  surprise_t* surprise = (surprise_t*)(*ctx);
    if (!surprise) return -1; // Memory allocation failed

    return 0;
}

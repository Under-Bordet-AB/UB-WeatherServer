#include "surprise.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "../../../global_defines.h"

// Map local names to central test configuration values
#define IMAGE_NAME Surprise_IMAGE_NAME // From global_defines.h (original: libs/backends/surprise/surprise.c)
#define SURPRISE_FOLDER Surprise_FOLDER // From global_defines.h (original: libs/backends/surprise/surprise.c)

int surprise_get_file(uint8_t **buffer_ptr, const char *file_name) {
  // Open specified file i mode "rb" - read binary
  char *folder_file = (char*)malloc(sizeof(char) * (strlen(SURPRISE_FOLDER) + strlen(file_name) + 1));
  sprintf(folder_file, "%s%s", SURPRISE_FOLDER, file_name);

  FILE *fptr = fopen(folder_file, "rb");
  free(folder_file);
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
    return -2;
  }

  *buffer_ptr = buffer;

  fclose(fptr);

  return file_size;
}

int surprise_get_random(uint8_t **buffer_ptr){
  tinydir_dir dir;
  tinydir_open_sorted(&dir, SURPRISE_FOLDER);

  if (dir.n_files == 0) { // Surprise folder not found
    tinydir_close(&dir);
    return -1;
  }

  int n_files = 0;
  for (int i = 0; i < dir.n_files; i++){
    n_files += dir._files[i].is_reg; // count number of files in folder
  }

  if (n_files == 0) {// Surprise folder is empty
    tinydir_close(&dir);
    return -2;
  }

  srand(time(NULL));
  int index = rand() % n_files + 1;

  for (int i = 0; i < dir.n_files; i++){
    index -= dir._files[i].is_reg; // Decrement index each time we iterate past a file
    if (index == 0){
      int result = surprise_get_file(buffer_ptr, dir._files[i].name);
      tinydir_close(&dir);
      return result;
    }
  }

  tinydir_close(&dir);

  return -1;
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
        surprise->bytesread = surprise_get_random(&surprise->buffer);
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
  if (!ctx || !*ctx) return 0; // Already disposed or NULL
  
  surprise_t* surprise = (surprise_t*)(*ctx);

  if (surprise->buffer) {
    free(surprise->buffer);
    surprise->buffer = NULL;
  }

  free(surprise);
  *ctx = NULL;

  return 0;
}

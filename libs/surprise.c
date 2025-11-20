#include "surprise.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define SURPRISE_FOLDER "./surprise/"

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
  }

  *buffer_ptr = buffer;

  return file_size;
}

int surprise_get_random(uint8_t **buffer_ptr){
  tinydir_dir dir;
  tinydir_open_sorted(&dir, SURPRISE_FOLDER);

  if (dir.n_files == 0) // Surprise folder not found
    return -1;

  int n_files = 0;
  for (int i = 0; i < dir.n_files; i++){
    n_files += dir._files[i].is_reg; // count number of files in folder
  }

  if (n_files == 0) // Surprise folder is empty
    return -2;

  srand(time(NULL));
  int index = rand() % n_files + 1;

  for (int i = 0; i < dir.n_files; i++){
    index -= dir._files[i].is_reg; // Decrement index each time we iterate past a file
    if (index == 0){
      return surprise_get_file(buffer_ptr, dir._files[i].name);
    }
  }

  return -1;
}
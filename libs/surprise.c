#include "surprise.h"

#include <stdio.h>
#include <stdlib.h>

int surprise_get_file(uint8_t **buffer_ptr, const char *file_name) {
  // Open specified file i mode "rb" - read binary
  FILE *fptr = fopen(file_name, "rb");
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
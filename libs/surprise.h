#ifndef SURPRISE_H
#define SURPRISE_H

#include <stdint.h>

int surprise_get_file(uint8_t **buffer_ptr, const char *file_name);

#endif
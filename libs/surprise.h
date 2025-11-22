#ifndef SURPRISE_H
#define SURPRISE_H

#include <stdint.h>
#include "tinydir.h"


int surprise_get_file(uint8_t **buffer_ptr, const char *file_name);
int surprise_get_random(uint8_t **buffer_ptr);


#endif
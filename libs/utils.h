#ifndef UTILS_H
#define UTILS_H

#include <stdint.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

uint64_t SystemMonotonicMS();

uint8_t* readFileToBuffer(const char* path, size_t* outSize);

#endif

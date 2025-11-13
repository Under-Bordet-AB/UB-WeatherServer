#ifndef UTILS_H
#define UTILS_H

#include <stdint.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

uint64_t SystemMonotonicMS();

uint8_t* readFileToBuffer(const char* path, size_t* outSize) {
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    uint8_t* buffer = malloc(size);
    if (!buffer) {
        fclose(f);
        return NULL;
    }

    fread(buffer, 1, size, f);
    fclose(f);

    *outSize = size;
    return buffer;
}

#endif

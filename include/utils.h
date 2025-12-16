#ifndef UTILS_H
#define UTILS_H

#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

uint64_t SystemMonotonicMS();

static inline int create_folder(const char* _Path) {
#if defined _WIN32
    bool success = CreateDirectory(_Path, NULL);
    if (success == false) {
        DWORD err = GetLastError();
        if (err == ERROR_ALREADY_EXISTS)
            return 1;
        else
            return -1;
    }
#else
    int success = mkdir(_Path, 0777);
    if (success != 0) {
        if (errno == EEXIST)
            return 1;
        else
            return -1;
    }
#endif

    return 0;
}

#endif

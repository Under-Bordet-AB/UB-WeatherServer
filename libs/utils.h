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

static char* create_uppercase_copy(const char* str) {
    if (!str) return NULL;

    size_t len = strlen(str);
    char* upper_str = malloc(len + 1);
    if (!upper_str) return NULL;

    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)str[i];

        // Handle UTF-8 encoded Swedish characters
        if (c == 0xC3 && i + 1 < len) {
            unsigned char next = (unsigned char)str[i + 1];

            switch (next) {
            case 0xA4: // ä (UTF-8: C3 A4)
                upper_str[i] = 0xC3;
                upper_str[i + 1] = 0x84; // Ä (UTF-8: C3 84)
                i++;                     // Skip next byte as we've processed it
                break;
            case 0xA5: // å (UTF-8: C3 A5)
                upper_str[i] = 0xC3;
                upper_str[i + 1] = 0x85; // Å (UTF-8: C3 85)
                i++;                     // Skip next byte as we've processed it
                break;
            case 0xB6: // ö (UTF-8: C3 B6)
                upper_str[i] = 0xC3;
                upper_str[i + 1] = 0x96; // Ö (UTF-8: C3 96)
                i++;                     // Skip next byte as we've processed it
                break;
            default:
                // Copy the C3 and let next iteration handle the following byte
                upper_str[i] = c;
                break;
            }
        } else {
            // Regular ASCII character
            upper_str[i] = toupper(c);
        }
    }

    // Null terminate
    upper_str[len] = '\0';

    return upper_str;
}

static char* create_lowercase_copy(const char* str) {
    if (!str) return NULL;

    size_t len = strlen(str);
    char* lower_str = malloc(len + 1);
    if (!lower_str) return NULL;

    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)str[i];

        // Handle UTF-8 encoded Swedish characters
        if (c == 0xC3 && i + 1 < len) {
            unsigned char next = (unsigned char)str[i + 1];

            switch (next) {
            case 0x84: // Ä (UTF-8: C3 84)
                lower_str[i] = 0xC3;
                lower_str[i + 1] = 0xA4; // ä (UTF-8: C3 A4)
                i++;                     // Skip next byte as we've processed it
                break;
            case 0x85: // Å (UTF-8: C3 85)
                lower_str[i] = 0xC3;
                lower_str[i + 1] = 0xA5; // å (UTF-8: C3 A5)
                i++;                     // Skip next byte as we've processed it
                break;
            case 0x96: // Ö (UTF-8: C3 96)
                lower_str[i] = 0xC3;
                lower_str[i + 1] = 0xB6; // ö (UTF-8: C3 B6)
                i++;                     // Skip next byte as we've processed it
                break;
            default:
                // Copy the C3 and let next iteration handle the following byte
                lower_str[i] = c;
                break;
            }
        } else {
            // Regular ASCII character
            lower_str[i] = tolower(c);
        }
    }

    // Null terminate
    lower_str[len] = '\0';

    return lower_str;
}

#endif

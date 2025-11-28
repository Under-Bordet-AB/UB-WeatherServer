#pragma once

#include "../utils/ui.h"
#include "global_defines.h"
#include "w_client.h"
#include "w_server.h"
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

// Load an image
static char* load_image(const char* path, size_t* out_size) {
    if (!path || !out_size) {
        errno = EINVAL;
        return NULL;
    }
    *out_size = 0;

    FILE* f = fopen(path, "rb");
    if (!f)
        return NULL; /* errno set by fopen */

    struct stat st;
    if (fstat(fileno(f), &st) != 0) {
        fclose(f);
        return NULL; /* errno set by fstat */
    }

    if (st.st_size <= 0) { /* empty file or directory / special file */
        fclose(f);
        errno = EIO;
        return NULL;
    }

    size_t file_size = (size_t)st.st_size;
    char* buf = malloc(file_size + 1); /* +1 for optional NUL */
    if (!buf) {
        fclose(f);
        errno = ENOMEM;
        return NULL;
    }

    size_t got = 0;
    while (got < file_size) {
        size_t r = fread(buf + got, 1, file_size - got, f);
        if (r == 0) {
            if (feof(f))
                break;
            if (ferror(f)) {
                free(buf);
                fclose(f);
                errno = EIO;
                return NULL;
            }
        }
        got += r;
    }
    fclose(f);

    if (got != file_size) {
        free(buf);
        errno = EIO;
        return NULL;
    }

    buf[file_size] = '\0'; /* safe NUL terminator */
    *out_size = file_size;
    return buf;
}
/** URL DECODING FUNCTION
 * Converts a URL-encoded string (e.g., "j%c3%b6nk%c3%b6ping") in-place
 * into its raw UTF-8 byte representation (e.g., "jönköping").
 * WARNING: The string buffer is modified IN-PLACE and the final string
 * will be SHORTER than or equal to the original length.
 * Note: Assumes input is valid, or the null-filtering logic is applied.
 */
static void utils_convert_utf8_hex_to_utf8_bytes(char* str) {
    if (str == NULL) {
        return;
    }
    char* read_ptr = str;
    char* write_ptr = str;

    while (*read_ptr) {
        if (*read_ptr == '%' && isxdigit((unsigned char)read_ptr[1]) && isxdigit((unsigned char)read_ptr[2])) {
            unsigned int byte_value;
            // Use sscanf to read the 2 hex digits immediately after '%'
            if (sscanf(read_ptr + 1, "%2x", &byte_value) == 1) {
                *write_ptr++ = (char)byte_value;
                read_ptr += 3;
                continue; // Move to next loop iteration
            }
        }
        // Handle regular character OR invalid/incomplete % sequence
        *write_ptr++ = *read_ptr++;
    }
    *write_ptr = '\0';
}

/** URL ENCODING FUNCTION
 * Converts a raw UTF-8 string (e.g., "jönköping") into its percent-encoded
 * representation (e.g., "j%C3%B6nk%C3%B6ping") for safe HTTP transfer.
 * WARNING: The result is written to a separate destination buffer, and the
 * final string will be LONGER than or equal to the original length.
 * @param src       The null-terminated raw UTF-8 string to encode.
 * @param dest      The destination buffer where the encoded string is written.
 * @param dest_size The maximum size of the destination buffer (for safety).
 * @return The length of the encoded string, or -1 if the destination buffer is too small.
 */
static int utils_convert_utf8_bytes_to_utf8_hex_encoding(const char* src, char* dest, size_t dest_size) {
    // These characters do not need encoding.
    // This set is often used for query parameter values.
    const char* safe_chars = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-._~";

    size_t dest_idx = 0;

    // Loop through the source string byte by byte
    for (size_t i = 0; src[i] != '\0'; i++) {
        unsigned char byte = (unsigned char)src[i];

        // 1. Check if the character is a "safe" ASCII character (no encoding needed)
        if (strchr(safe_chars, byte) != NULL) {

            if (dest_idx + 1 >= dest_size)
                return -1; // Check space for 1 byte + \0
            dest[dest_idx++] = byte;
        }
        // 2. Check for Space (' ') - often encoded as '+' or '%20'. We use %20.
        else if (byte == ' ') {

            if (dest_idx + 3 >= dest_size)
                return -1; // Check space for %20 + \0
            // Using sprintf is a quick way to format the hex bytes
            sprintf(dest + dest_idx, "%%20");
            dest_idx += 3;
        }
        // 3. Encode ALL other bytes (including multi-byte UTF-8, reserved ASCII, etc.)
        else {

            if (dest_idx + 3 >= dest_size)
                return -1; // Check space for %XX + \0

            // Format the byte as %XX hexadecimal and write it to the buffer
            // The first '%' is escaped as '%%', the second is the format specifier
            sprintf(dest + dest_idx, "%%%02X", byte);
            dest_idx += 3;
        }
    }

    // Null-terminate the destination string
    dest[dest_idx] = '\0';
    return dest_idx;
}

// Lowercase ASCII + percent-encoded ASCII + Swedish ÅÄÖ in UTF-8
static void utils_to_lowercase(char* s) {
    if (!s)
        return;
    size_t len = strlen(s);
    if (len > MAX_URL_LEN)
        len = MAX_URL_LEN; // safety limit
    for (size_t i = 0; i < len; i++) {
        char* c = &s[i];
        // 1. Normal ASCII letters
        if (*c >= 'A' && *c <= 'Z') {
            *c = *c + 0x20; // A→a, etc.
        }

        // 2. Percent-encoded sequences
        if (*c == '%' && i + 2 < len && isxdigit((unsigned char)s[i + 1]) && isxdigit((unsigned char)s[i + 2])) {

            // Lowercase the hex digits
            if (s[i + 1] >= 'A' && s[i + 1] <= 'F')
                s[i + 1] += 0x20;
            if (s[i + 2] >= 'A' && s[i + 2] <= 'F')
                s[i + 2] += 0x20;

            // Detect %c3 (first byte of UTF-8 ÅÄÖ)
            if (s[i + 1] == 'c' && s[i + 2] == '3') {

                // Look ahead to get the next %xx
                if (i + 5 < len && s[i + 3] == '%' && isxdigit((unsigned char)s[i + 4]) &&
                    isxdigit((unsigned char)s[i + 5])) {

                    // Decode hex digit
                    char hi = tolower(s[i + 4]);
                    char lo = tolower(s[i + 5]);

                    int byte = (hi <= '9' ? hi - '0' : hi - 'a' + 10) * 16 + (lo <= '9' ? lo - '0' : lo - 'a' + 10);

                    // Uppercase Swedish UTF-8 second bytes
                    if (byte == 0x85 || byte == 0x84 || byte == 0x96) {
                        byte += 0x20; // Lowercase

                        static const char hex[] = "0123456789abcdef";
                        s[i + 4] = hex[(byte >> 4) & 0xF];
                        s[i + 5] = hex[byte & 0xF];
                    }
                }
            }

            i += 2; // Skip %xx
        }
    }
}

/* Normalize some Swedish variant confusion: map Å/å -> Ä/ä for geocode queries
 * This function modifies the UTF-8 byte sequences in-place. It replaces
 * two-byte sequences 0xC3 0x85 and 0xC3 0xA5 (Å and å) with 0xC3 0xA4 (ä).
 * Use this only when you want to canonicalize user input for matching
 * systems that expect 'ä' instead of 'å' in specific names.
 */
static void utils_normalize_swedish_a_umlaut(char* str) {
    if (!str)
        return;
    for (size_t i = 0; str[i]; i++) {
        unsigned char c = (unsigned char)str[i];
        if (c == 0xC3 && str[i + 1]) {
            unsigned char n = (unsigned char)str[i + 1];
            if (n == 0x85 || n == 0xA5) {
                /* Å (0xC3 0x85) or å (0xC3 0xA5) -> ä (0xC3 0xA4) */
                str[i + 1] = (char)0xA4;
                i += 1; /* skip the second byte */
            }
        }
    }
}

/* Lower-case only common Swedish two-byte uppercase letters in-place.
 * This does not touch ASCII letters; use this when you want to preserve
 * ASCII case but normalize Å/Ä/Ö -> å/ä/ö so downstream systems receive
 * consistent UTF-8 sequences.
 */
static void utils_lowercase_swedish_letters(char* str) {
    if (!str)
        return;
    for (size_t i = 0; str[i]; i++) {
        unsigned char c = (unsigned char)str[i];
        if (c == 0xC3 && str[i + 1]) {
            unsigned char n = (unsigned char)str[i + 1];
            if (n == 0x85) { /* Å -> å */
                str[i + 1] = (char)0xA5;
                i += 1;
            } else if (n == 0x84) { /* Ä -> ä */
                str[i + 1] = (char)0xA4;
                i += 1;
            } else if (n == 0x96) { /* Ö -> ö */
                str[i + 1] = (char)0xB6;
                i += 1;
            }
        }
    }
}

/*
 * s owe can printf them and match against swedish like "Jönköping" the given number of milliseconds.
 *
 * @param ms  Milliseconds to sleep. 0 <= ms <= INT_MAX/1000.
 *            The function may return earlier if interrupted by a signal.
 */

static inline void utils_sleep_ms(unsigned int ms) {
    struct timespec req = {.tv_sec = ms / 1000, .tv_nsec = (ms % 1000) * 1000000L};

    /* Keep sleeping until the entire interval has elapsed. */
    while (nanosleep(&req, &req) == -1 && errno == EINTR)
        ; // interrupted by signal; resume with remaining time in `req`
}

/**
 * Clears the terminal screen.
 */
static inline void utils_clear_screen() {
    system("clear");
}

/**
 * Prints a formatted banner with a message.
 * Example: [INFO] Server started
 */
static inline void utils_print_banner(const char* message) {
    printf("========================================\n");
    printf("[INFO] %s\n", message);
    printf("========================================\n");
}

/**
 * Creates a directory if it doesn't exist.
 * @param path The directory path to create
 * @return 0 on success, -1 on failure
 */
static inline int create_folder(const char* path) {
    if (mkdir(path, 0755) == 0 || errno == EEXIST) {
        return 0;
    }
    return -1;
}

///// Socket helper functions

// TODO move this to lib?
// Bind a listening socket, specific for server since we set AI_PASSIVE, SO_REUSEADDR  LISTENING
static inline int w_server_bind_listening_socket(int* fd, char* address, char* port, int backlog) {
    int enable = 1; // for setting options
    struct addrinfo* res = NULL;
    struct addrinfo* p = NULL;
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    // options for gai
    hints.ai_family = AF_UNSPEC;     // try to bind to both IPv4 and IPv6
    hints.ai_socktype = SOCK_STREAM; // TCP (other option is UDP)
    hints.ai_flags = AI_PASSIVE;     // So we can bind()  (other option is connect(), for clients)

    // gai returns a linked list with all addresses
    int gai_err = getaddrinfo(address[0] ? address : NULL, port, &hints, &res);
    if (gai_err != 0) {
        ui_print_server_init_error(gai_strerror(gai_err));
        if (res != NULL) {
            freeaddrinfo(res);
        }
        return W_SERVER_ERROR_GETADDRINFO;
    }

    // Loop the list and try to bind, stop at first succesful bind.
    for (p = res; p != NULL; p = p->ai_next) {
        // get a socket from the OS
        *fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (*fd == -1) {
            continue;
        }

        // Allows quick port reuse. Must set before bind(), it changes how OS binds.
        setsockopt(*fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));

        // Try to bind address to the socket
        if (bind(*fd, p->ai_addr, p->ai_addrlen) == -1) {
            close(*fd);
            *fd = -1;
            continue; // fail, try next
        }
        break;
    }
    // Done with linked list here
    freeaddrinfo(res);

    // Did we fail to bind?
    if (*fd < 0) {
        ui_print_server_init_error("W_SERVER_ERROR_SOCKET_BIND");
        return W_SERVER_ERROR_SOCKET_BIND;
    }

    // Success!
    // Disable Nagle's algorithm. We want segments sent immediately.
    if (setsockopt(*fd, IPPROTO_TCP, TCP_NODELAY, &enable, sizeof(enable)) == -1) {
        perror("setsockopt TCP_NODELAY");
        // Not critical enough to fail, but good to warn.
    }

    // set non blocking
    int flags = fcntl(*fd, F_GETFL, 0);
    if (fcntl(*fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        perror("fcntl F_SETFL");
        close(*fd);
        *fd = -1;
        return W_SERVER_ERROR_SET_NONBLOCKING;
    }

    // Set as listening
    if (listen(*fd, backlog) < 0) {
        perror("listen");
        close(*fd);
        *fd = -1;
        return W_SERVER_ERROR_SOCKET_LISTEN;
    }

    // Only success return
    return 0;
}
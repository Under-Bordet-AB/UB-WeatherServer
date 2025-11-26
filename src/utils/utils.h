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
// Converts string IN PLACE so we can printf them and match strings with "åäö"
static void utils_decode_swedish_chars(char* str) {
    if (str == NULL) {
        return;
    }
    char* read_ptr = str;
    char* write_ptr = str;

    while (*read_ptr) {
        if (*read_ptr == '%') {
            if (isxdigit((unsigned char)read_ptr[1]) && isxdigit((unsigned char)read_ptr[2])) {
                char hex_digit[3];
                hex_digit[0] = read_ptr[1];
                hex_digit[1] = read_ptr[2];
                hex_digit[2] = '\0';

                long byte_value = strtol(hex_digit, NULL, 16);
                *write_ptr = (char)byte_value;
                read_ptr += 3;
            } else {
                *write_ptr = *read_ptr;
                read_ptr++;
            }
        } else {
            // Regular character
            *write_ptr = *read_ptr;
            read_ptr++;
        }
        write_ptr++;
    }
    *write_ptr = '\0';
}

static void utils_to_lowercase(char* str) {
    if (str == NULL) {
        return;
    }
    for (int i = 0; str[i] != '\0'; i++) {
        unsigned char c = (unsigned char)str[i];
        if (c < 128) {
            str[i] = (char)tolower(c);
        } else {
            /* Handle a few common Swedish uppercase letters in UTF-8
             * Å (U+00C5) : 0xC3 0x85 -> å (U+00E5) : 0xC3 0xA5
             * Ä (U+00C4) : 0xC3 0x84 -> ä (U+00E4) : 0xC3 0xA4
             * Ö (U+00D6) : 0xC3 0x96 -> ö (U+00F6) : 0xC3 0xB6
             * We only lower-case these specific two-byte sequences to avoid
             * implementing full Unicode case-folding.
             */
            unsigned char n = (unsigned char)str[i + 1];
            if (c == 0xC3 && n != '\0') {
                if (n == 0x85) { /* Å */
                    str[i] = (char)0xC3;
                    str[i + 1] = (char)0xA5; /* å */
                    i += 1;                  /* consumed two bytes */
                    continue;
                } else if (n == 0x84) { /* Ä */
                    str[i] = (char)0xC3;
                    str[i + 1] = (char)0xA4; /* ä */
                    i += 1;
                    continue;
                } else if (n == 0x96) { /* Ö */
                    str[i] = (char)0xC3;
                    str[i + 1] = (char)0xB6; /* ö */
                    i += 1;
                    continue;
                }
            }
            /* leave other multibyte sequences untouched */
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
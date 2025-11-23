#pragma once

#include "../utils/ui.h"
#include "global_defines.h"
#include "w_client.h"
#include "w_server.h"
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

/**
 * Sleep for the given number of milliseconds.
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
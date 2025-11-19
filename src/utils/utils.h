#pragma once

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <time.h>
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

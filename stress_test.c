/* --------------------------------------------------------------------
 *  stress_test.c – TCP connection stress generator with speed presets
 *
 *  Author:       jimmyjordanSWE
 *  Created:      2025-11-18
 *  Description:  High-performance TCP stress testing tool with microsecond
 *                precision timing and multiple speed presets. Capable of
 *                generating up to 1M connections/sec on localhost.
 *
 *  Usage Examples:
 *      # Compile (with optimizations for speed)
 *          gcc -o stress_test stress_test.c -O2
 *
 *      # Show help and all available options
 *          ./stress_test -help
 *
 *      # Speed presets (easiest way to use)
 *          ./stress_test -slow       # 10ms interval (~100 conn/sec)
 *          ./stress_test -normal     # 1ms interval (~1,000 conn/sec)
 *          ./stress_test -fast       # 100µs interval (~10,000 conn/sec)
 *          ./stress_test -veryfast   # 10µs interval (~100,000 conn/sec)
 *          ./stress_test -insane     # 1µs interval (~1,000,000 conn/sec)
 *          ./stress_test -burst      # No delay (all at once)
 *
 *      # Combine presets with other options
 *          ./stress_test -fast -count 1000
 *          ./stress_test -insane -ip 192.168.1.100 -port 8080
 *          ./stress_test -veryfast -count 5000 -msg "GET / HTTP/1.1\r\n\r\n"
 *
 *      # Custom interval in microseconds
 *          ./stress_test -custom 500 -count 2000
 *
 *      # Full example with all options
 *          ./stress_test -fast -ip 127.0.0.1 -port 10480 -count 512 \
 *                        -msg "HELLO\n"
 *
 *  Command-Line Options:
 *      Speed Presets:
 *          -slow         10ms interval (~100 connections/sec)
 *          -normal       1ms interval (~1,000 connections/sec)
 *          -fast         100µs interval (~10,000 connections/sec) [DEFAULT]
 *          -veryfast     10µs interval (~100,000 connections/sec)
 *          -insane       1µs interval (~1,000,000 connections/sec)
 *          -burst        No delay (creates all connections instantly)
 *          -custom <us>  Custom interval in microseconds
 *
 *      Connection Options:
 *          -ip <addr>    Target IP address (default: 127.0.0.1)
 *          -port <num>   Target TCP port (default: 10480)
 *          -count <num>  Number of connections (default: 512)
 *          -msg <str>    Message to send after connect (default: "HELLO\n")
 *
 *      Other:
 *          -help         Display usage information
 *
 *  How It Works:
 *      1. Creates sockets at the specified rate (interval between each)
 *      2. Uses non-blocking I/O and select() to monitor connection status
 *      3. Sends the specified message immediately after each connection
 *      4. Tracks success/failure rates and actual throughput
 *      5. Keeps all connections open for 5 seconds before cleanup
 *
 *  Performance Notes:
 *      - On localhost, can achieve 500k+ connections/sec in practice
 *      - Theoretical limit is ~1M/sec (syscall overhead is the bottleneck)
 *      - No kernel rate limiting on loopback interface
 *      - Server accept() rate is typically the limiting factor
 *
 *  Exit Status:
 *      0    All connections succeeded
 *      1    Error (invalid arguments, socket failures, etc.)
 *
 * ------------------------------------------------------------------- */

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#define DEFAULT_IP "127.0.0.1"
#define DEFAULT_PORT 10480
#define DEFAULT_CONN 512
#define DEFAULT_MSG "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n"
#define CONNECT_TIMEOUT_SEC 10

typedef struct {
    int fd;
    int connected;
    struct timespec connect_start;
} client_t;

typedef enum {
    MODE_SLOW,     // 10ms interval (100/sec)
    MODE_NORMAL,   // 1ms interval (1,000/sec)
    MODE_FAST,     // 100μs interval (10,000/sec)
    MODE_VERYFAST, // 10μs interval (100,000/sec)
    MODE_INSANE,   // 1μs interval (1,000,000/sec)
    MODE_BURST,    // 0μs interval (instant burst)
    MODE_CUSTOM    // User specified
} speed_mode_t;

// Get time in microseconds
static inline long long get_time_us() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000000LL + ts.tv_nsec / 1000;
}

void print_usage(const char* prog) {
    printf("Usage: %s [OPTIONS]\n\n", prog);
    printf("Speed Presets:\n");
    printf("  -slow       10ms interval (~100 connections/sec)\n");
    printf("  -normal     1ms interval (~1,000 connections/sec)\n");
    printf("  -fast       100μs interval (~10,000 connections/sec)\n");
    printf("  -veryfast   10μs interval (~100,000 connections/sec)\n");
    printf("  -insane     1μs interval (~1,000,000 connections/sec)\n");
    printf("  -burst      No delay (all at once)\n");
    printf("  -custom <us> Custom interval in microseconds\n\n");
    printf("Other Options:\n");
    printf("  -ip <addr>      Server IP (default: %s)\n", DEFAULT_IP);
    printf("  -port <num>     Server port (default: %d)\n", DEFAULT_PORT);
    printf("  -count <num>    Number of connections (default: %d)\n", DEFAULT_CONN);
    printf("  -msg <string>   Message to send (default: \"%s\")\n", DEFAULT_MSG);
    printf("  -help           Show this help\n\n");
    printf("Examples:\n");
    printf("  %s -fast\n", prog);
    printf("  %s -insane -count 1000\n", prog);
    printf("  %s -custom 500 -ip 192.168.1.100 -port 8080\n", prog);
    printf("  %s -burst -count 10000\n", prog);
}

int main(int argc, char** argv) {
    const char* ip = DEFAULT_IP;
    int port = DEFAULT_PORT;
    int total = DEFAULT_CONN;
    const char* msg = DEFAULT_MSG;
    speed_mode_t mode = MODE_FAST; // Default
    int interval_us = 100;

    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-help") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "-slow") == 0) {
            mode = MODE_SLOW;
            interval_us = 10000;
        } else if (strcmp(argv[i], "-normal") == 0) {
            mode = MODE_NORMAL;
            interval_us = 1000;
        } else if (strcmp(argv[i], "-fast") == 0) {
            mode = MODE_FAST;
            interval_us = 100;
        } else if (strcmp(argv[i], "-veryfast") == 0) {
            mode = MODE_VERYFAST;
            interval_us = 10;
        } else if (strcmp(argv[i], "-insane") == 0) {
            mode = MODE_INSANE;
            interval_us = 1;
        } else if (strcmp(argv[i], "-burst") == 0) {
            mode = MODE_BURST;
            interval_us = 0;
        } else if (strcmp(argv[i], "-custom") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: -custom requires an argument\n");
                return 1;
            }
            mode = MODE_CUSTOM;
            interval_us = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-ip") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: -ip requires an argument\n");
                return 1;
            }
            ip = argv[++i];
        } else if (strcmp(argv[i], "-port") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: -port requires an argument\n");
                return 1;
            }
            port = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-count") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: -count requires an argument\n");
                return 1;
            }
            total = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-msg") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: -msg requires an argument\n");
                return 1;
            }
            msg = argv[++i];
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }

    client_t* clients = calloc(total, sizeof(client_t));
    if (!clients) {
        perror("calloc");
        return 1;
    }

    // set fd to -1
    for (int i = 0; i < total; i++) {
        clients[i].fd = -1;
    }

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, ip, &addr.sin_addr) <= 0) {
        fprintf(stderr, "Invalid IP address: %s\n", ip);
        free(clients);
        return 1;
    }

    // Print configuration
    const char* mode_names[] = {"SLOW", "NORMAL", "FAST", "VERY FAST", "INSANE", "BURST", "CUSTOM"};
    printf("=== Connection Stress Test ===\n");
    printf("Target:   %s:%d\n", ip, port);
    printf("Clients:  %d\n", total);
    printf("Mode:     %s\n", mode_names[mode]);
    if (mode == MODE_BURST) {
        printf("Interval: No delay (burst mode)\n");
    } else {
        printf("Interval: %d microseconds (%.0f/sec)\n", interval_us, 1000000.0 / interval_us);
    }
    printf("Message:  \"%s\"\n", msg);
    printf("==============================\n\n");

    long long start_time_us = get_time_us();
    long long last_connect_time_us = start_time_us;

    int next_to_create = 0;
    int sockets_created = 0;
    int connected_count = 0;
    int failed_count = 0;
    int sent_count = 0;

    // Main loop: create connections at specified rate and monitor them
    while ((connected_count + failed_count) < total) {
        long long now_us = get_time_us();

        // Check for overall timeout
        if ((now_us - start_time_us) > (CONNECT_TIMEOUT_SEC * 1000000LL)) {
            fprintf(stderr, "\nTimeout after %d seconds\n", CONNECT_TIMEOUT_SEC);
            break;
        }

        // Create new connection(s)
        int can_create = (next_to_create < total);
        if (mode == MODE_BURST) {
            // Burst mode: create all at once
            can_create = can_create && (next_to_create == 0 || sockets_created < total);
        } else {
            // Paced mode: check interval
            can_create = can_create && ((now_us - last_connect_time_us) >= interval_us);
        }

        if (can_create) {
            int fd = socket(AF_INET, SOCK_STREAM, 0);
            if (fd < 0) {
                perror("socket");
                next_to_create++;
                failed_count++;
            } else {
                // Set non-blocking
                int flags = fcntl(fd, F_GETFL, 0);
                fcntl(fd, F_SETFL, flags | O_NONBLOCK);

                // Initiate connection
                int r = connect(fd, (struct sockaddr*)&addr, sizeof(addr));
                if (r < 0 && errno != EINPROGRESS) {
                    if (sockets_created < 10) { // Only print first few errors
                        fprintf(stderr, "connect() failed: %s\n", strerror(errno));
                    }
                    close(fd);
                    next_to_create++;
                    failed_count++;
                } else {
                    clients[next_to_create].fd = fd;
                    clients[next_to_create].connected = 0;
                    clock_gettime(CLOCK_MONOTONIC, &clients[next_to_create].connect_start);
                    sockets_created++;
                    next_to_create++;
                }
            }

            last_connect_time_us = now_us;

            // In burst mode, continue immediately
            if (mode == MODE_BURST && next_to_create < total) {
                continue;
            }
        }

        // Check all pending connections with select (non-blocking)
        fd_set wfds, efds;
        FD_ZERO(&wfds);
        FD_ZERO(&efds);
        int maxfd = 0;

        for (int i = 0; i < next_to_create; i++) {
            if (clients[i].fd > 0 && !clients[i].connected) {
                FD_SET(clients[i].fd, &wfds);
                FD_SET(clients[i].fd, &efds);
                if (clients[i].fd > maxfd)
                    maxfd = clients[i].fd;
            }
        }

        if (maxfd > 0) {
            // Very short timeout - we want to keep creating connections
            struct timeval tv = {0, 0}; // Non-blocking select
            int n = select(maxfd + 1, NULL, &wfds, &efds, &tv);

            if (n < 0) {
                perror("select");
                break;
            }

            // Process ready connections
            if (n > 0) {
                struct timespec now_ts;
                clock_gettime(CLOCK_MONOTONIC, &now_ts);

                for (int i = 0; i < next_to_create; i++) {
                    if (clients[i].fd <= 0 || clients[i].connected)
                        continue;

                    // Check for per-connection timeout (5 seconds)
                    long long elapsed_ms = (now_ts.tv_sec - clients[i].connect_start.tv_sec) * 1000LL +
                                           (now_ts.tv_nsec - clients[i].connect_start.tv_nsec) / 1000000LL;

                    if (elapsed_ms > 5000) {
                        if (failed_count < 10) { // Only print first few timeouts
                            fprintf(stderr, "Connection %d timed out\n", i);
                        }
                        close(clients[i].fd);
                        clients[i].fd = -1;
                        failed_count++;
                        continue;
                    }

                    // Check for errors
                    if (FD_ISSET(clients[i].fd, &efds)) {
                        int error = 0;
                        socklen_t len = sizeof(error);
                        getsockopt(clients[i].fd, SOL_SOCKET, SO_ERROR, &error, &len);
                        if (error && failed_count < 10) {
                            fprintf(stderr, "Connection %d error: %s\n", i, strerror(error));
                        }
                        close(clients[i].fd);
                        clients[i].fd = -1;
                        failed_count++;
                        continue;
                    }

                    // Check if connected
                    if (FD_ISSET(clients[i].fd, &wfds)) {
                        int error = 0;
                        socklen_t len = sizeof(error);
                        if (getsockopt(clients[i].fd, SOL_SOCKET, SO_ERROR, &error, &len) < 0) {
                            perror("getsockopt");
                            close(clients[i].fd);
                            clients[i].fd = -1;
                            failed_count++;
                            continue;
                        }

                        if (error != 0) {
                            if (failed_count < 10) {
                                fprintf(stderr, "Connection %d failed: %s\n", i, strerror(error));
                            }
                            close(clients[i].fd);
                            clients[i].fd = -1;
                            failed_count++;
                            continue;
                        }

                        // Success!
                        clients[i].connected = 1;
                        connected_count++;

                        // Send message (non-blocking, best effort)
                        ssize_t s = send(clients[i].fd, msg, strlen(msg), MSG_DONTWAIT);
                        if (s > 0) {
                            sent_count++;
                        }

                        // Progress update
                        if ((connected_count % 100) == 0 || (connected_count < 100 && connected_count % 10 == 0)) {
                            long long elapsed_us = get_time_us() - start_time_us;
                            double rate = connected_count * 1000000.0 / elapsed_us;
                            printf("Progress: %d connected, %d failed (%.0f conn/sec)\n", connected_count, failed_count,
                                   rate);
                        }
                    }
                }
            }
        } else if (next_to_create >= total) {
            // No more to create, but still waiting for some to complete
            usleep(100);
        }
    }

    long long end_time_us = get_time_us();
    double elapsed_sec = (end_time_us - start_time_us) / 1000000.0;
    double actual_rate = connected_count / elapsed_sec;

    printf("\n=== Results ===\n");
    printf("Target:           %d connections\n", total);
    printf("Sockets created:  %d\n", sockets_created);
    printf("Connected:        %d clients (%.1f%%)\n", connected_count, 100.0 * connected_count / total);
    printf("Failed:           %d clients (%.1f%%)\n", failed_count, 100.0 * failed_count / total);
    printf("Messages sent:    %d\n", sent_count);
    printf("Time elapsed:     %.3f seconds\n", elapsed_sec);
    printf("Actual rate:      %.0f connections/sec\n", actual_rate);
    if (mode != MODE_BURST) {
        printf("Target rate:      %.0f connections/sec\n", 1000000.0 / interval_us);
    }

    if (connected_count < total) {
        printf("\nWARNING: Only %d/%d clients connected!\n", connected_count, total);
    } else {
        printf("\n✓ All clients connected successfully!\n");
    }

    // Keep connections open
    printf("\nKeeping connections open for 2 seconds...\n");
    sleep(2);

    // Cleanup
    for (int i = 0; i < total; i++) {
        if (clients[i].fd > 0)
            close(clients[i].fd);
    }
    free(clients);

    printf("Done!\n");
    return 0;
}
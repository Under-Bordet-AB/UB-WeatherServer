/* --------------------------------------------------------------------
 *  stress_test_enhanced.c – Realistic REST API stress testing tool
 *
 *  Author:       GitHub Copilot
 *  Created:      2025-11-19
 *  Description:  Enhanced stress testing tool that simulates real client
 *                behavior including response reading, timing metrics,
 *                varied traffic patterns, and comprehensive statistics.
 *
 *  Features:
 *      - Reads and parses HTTP responses
 *      - Tracks latency (connect, send, response, total)
 *      - Multiple request types (GET, POST, random endpoints)
 *      - Realistic timing with random think time
 *      - Percentile metrics (p50, p95, p99)
 *      - Response validation and status code tracking
 *      - Graceful connection handling
 *
 *  Usage:
 *      ./stress -fast -count 100 -ip 127.0.0.1 -port 10480
 *      ./stress -burst -realistic -count 1000
 *      ./stress -h
 *
 * ------------------------------------------------------------------- */

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/ttydefaults.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#define DEFAULT_IP "127.0.0.1"
#define DEFAULT_PORT 10480
#define DEFAULT_CONN 100
#define CONNECT_TIMEOUT_SEC 10
#define RESPONSE_TIMEOUT_SEC 10 // Increased for realistic mode with think time
#define MAX_RESPONSE_SIZE 65536

// Get terminal width, fallback to 80 if unable to detect
static int get_terminal_width() {
    struct winsize w;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0 && w.ws_col > 0) {
        return w.ws_col;
    }
    return 80; // fallback
}

// City data structure for weather requests
typedef struct {
    const char* name;
    double latitude;
    double longitude;
} city_t;

// Top Swedish cities with GPS coordinates
static const city_t CITIES[] = {
    {"Stockholm", 59.3293, 18.0686},  {"Gothenburg", 57.7089, 11.9746},   {"Malmö", 55.6049, 13.0038},
    {"Uppsala", 59.8586, 17.6389},    {"Linköping", 58.4108, 15.6214},    {"Örebro", 59.2741, 15.2066},
    {"Västerås", 59.6099, 16.5448},   {"Helsingborg", 56.0465, 12.6944},  {"Norrköping", 58.5877, 16.1924},
    {"Jönköping", 57.7826, 14.1618},  {"Umeå", 63.8258, 20.2630},         {"Lund", 55.7047, 13.1910},
    {"Borås", 57.7210, 12.9401},      {"Eskilstuna", 59.3712, 16.5098},   {"Gävle", 60.6745, 17.1417},
    {"Södertälje", 59.1955, 17.6253}, {"Karlstad", 59.3793, 13.5036},     {"Sundsvall", 62.3908, 17.3069},
    {"Luleå", 65.5848, 22.1567},      {"Östersund", 63.1767, 14.6361},    {"Växjö", 56.8790, 14.8059},
    {"Halmstad", 56.6745, 12.8571},   {"Kristianstad", 56.0313, 14.1524}, {"Falun", 60.6036, 15.6259},
    {"Kalmar", 56.6634, 16.3568},     {"Skövde", 58.3912, 13.8451},       {"Trollhättan", 58.2837, 12.2886},
    {"Uddevalla", 58.3498, 11.9356}};
#define NUM_CITIES (sizeof(CITIES) / sizeof(CITIES[0]))

// Request templates for backend testing
static const char* REQUEST_TEMPLATES[] = {
    NULL, // Weather template - dynamically generated
    "GET /GetCities HTTP/1.1\r\n"
    "Host: localhost\r\n"
    "User-Agent: StressTest/1.0\r\n"
    "Accept: application/json\r\n"
    "Connection: close\r\n\r\n",

    "GET /GetSurprise HTTP/1.1\r\n"
    "Host: localhost\r\n"
    "User-Agent: StressTest/1.0\r\n"
    "Accept: image/png\r\n"
    "Connection: close\r\n\r\n",
};
#define NUM_REQUEST_TYPES 3

typedef enum {
    CLIENT_CREATED,
    CLIENT_CONNECTING,
    CLIENT_CONNECTED,
    CLIENT_SENDING,
    CLIENT_SENT,
    CLIENT_RECEIVING,
    CLIENT_DONE,
    CLIENT_FAILED
} client_state_t;

typedef enum {
    MODE_SLOW,     // 10ms interval
    MODE_NORMAL,   // 1ms interval
    MODE_FAST,     // 100μs interval
    MODE_VERYFAST, // 10μs interval
    MODE_INSANE,   // 1μs interval
    MODE_BURST,    // No delay
    MODE_CUSTOM
} speed_mode_t;

typedef struct {
    int fd;
    client_state_t state;
    int request_type;
    int city_index; // For weather requests - which city to query

    // Timing
    struct timespec create_time;
    struct timespec connect_start;
    struct timespec connect_end;
    struct timespec send_start;
    struct timespec send_end;
    struct timespec recv_start;
    struct timespec recv_end;

    // Request/Response
    char* request_data; // Changed to char* for dynamic allocation
    size_t request_size;
    size_t sent_bytes;

    char* response_buffer;
    size_t response_capacity;
    size_t response_bytes;
    int http_status;

    // Metrics
    long long connect_time_us;
    long long send_time_us;
    long long response_time_us;
    long long total_time_us;

    int think_time_ms; // Random delay before sending
} client_t;

// Get time in microseconds
static inline long long get_time_us() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000000LL + ts.tv_nsec / 1000;
}

// Calculate time difference in microseconds
static inline long long timespec_diff_us(struct timespec* start, struct timespec* end) {
    return (end->tv_sec - start->tv_sec) * 1000000LL + (end->tv_nsec - start->tv_nsec) / 1000;
}

// Parse HTTP status code from response
static int parse_http_status(const char* response, size_t len) {
    if (len < 12)
        return 0;
    if (strncmp(response, "HTTP/1.", 7) != 0)
        return 0;

    // Find first space after HTTP/1.x
    const char* status_start = strchr(response + 7, ' ');
    if (!status_start)
        return 0;

    return atoi(status_start + 1);
}

// Comparison function for qsort
static int compare_long_long(const void* a, const void* b) {
    long long diff = *(long long*)a - *(long long*)b;
    return (diff > 0) - (diff < 0);
}

// Calculate percentile from sorted array
static long long calculate_percentile(long long* sorted_array, int count, double percentile) {
    if (count == 0)
        return 0;
    int index = (int)((count - 1) * percentile / 100.0);
    return sorted_array[index];
}

void print_usage(const char* prog) {
    printf("Usage: %s [OPTIONS]\n\n", prog);
    printf("Enhanced REST API Stress Test for Weather Server Backends\n\n");
    printf("Speed Modes:\n");
    printf("  -slow       200ms interval (~5 req/sec)\n");
    printf("  -normal     1ms interval (~1,000 req/sec)\n");
    printf("  -fast       100μs interval (~10,000 req/sec)\n");
    printf("  -veryfast   10μs interval (~100,000 req/sec)\n");
    printf("  -insane     1μs interval (~1,000,000 req/sec)\n");
    printf("  -burst      No delay (all requests at once)\n");
    printf("  -custom <us> Custom interval in microseconds\n");
    printf("              [DEFAULT: trickle mode, 250ms intervals (~4 req/sec)]\n\n");
    printf("Backend Selection:\n");
    printf("  -weather        Test weather backend (cycles through major Swedish cities)\n");
    printf("  -cities         Test cities backend (/GetCities)\n");
    printf("  -surprise       Test surprise backend (/GetSurprise)\n");
    printf("                  [DEFAULT: test all backends if none specified]\n\n");
    printf("Options:\n");
    printf("  -ip <addr>      Server IP or hostname (default: %s)\n", DEFAULT_IP);
    printf("  -port <num>     Server port (default: %d)\n", DEFAULT_PORT);
    printf("  -count <num>    Number of requests (default: %d)\n", DEFAULT_CONN);
    printf("  -realistic      Add random think time (100-500ms) after connection\n");
    printf("  -keepalive <s>  Keep connections open for N seconds (default: 0)\n");
    printf("  -h, -help       Show this help\n\n");
    printf("Examples:\n");
    printf("  %s -count 100 -weather                    # Test weather backend with trickle\n", prog);
    printf("  %s -count 50 -cities -surprise            # Test cities and surprise backends\n", prog);
    printf("  %s -fast -weather -cities -surprise       # Fast test of all backends\n", prog);
    printf("  %s -burst -count 1000 -realistic          # Burst test with think time\n", prog);
    printf("  %s -custom 500000 -count 20 -surprise     # Custom 500ms intervals\n", prog);
}

int main(int argc, char** argv) {
    const char* ip = DEFAULT_IP;
    int port = DEFAULT_PORT;
    int total = DEFAULT_CONN;
    speed_mode_t mode = MODE_CUSTOM; // Default to custom trickle mode
    int interval_us = 250000;        // 250ms default trickle rate
    int realistic_timing = 0;
    int keepalive_sec = 0;

    // Backend selection flags
    int test_weather = 0;
    int test_cities = 0;
    int test_surprise = 0;

    // Parse arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "-help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "-slow") == 0) {
            mode = MODE_SLOW;
            interval_us = 200000; // 200ms intervals
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
            if (++i >= argc) {
                fprintf(stderr, "Error: -custom requires an argument\n");
                return 1;
            }
            mode = MODE_CUSTOM;
            interval_us = atoi(argv[i]);
        } else if (strcmp(argv[i], "-ip") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "Error: -ip requires an argument\n");
                return 1;
            }
            ip = argv[i];
        } else if (strcmp(argv[i], "-port") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "Error: -port requires an argument\n");
                return 1;
            }
            port = atoi(argv[i]);
        } else if (strcmp(argv[i], "-count") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "Error: -count requires an argument\n");
                return 1;
            }
            total = atoi(argv[i]);
        } else if (strcmp(argv[i], "-realistic") == 0) {
            realistic_timing = 1;
        } else if (strcmp(argv[i], "-weather") == 0) {
            test_weather = 1;
        } else if (strcmp(argv[i], "-cities") == 0) {
            test_cities = 1;
        } else if (strcmp(argv[i], "-surprise") == 0) {
            test_surprise = 1;
        } else if (strcmp(argv[i], "-keepalive") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "Error: -keepalive requires an argument\n");
                return 1;
            }
            keepalive_sec = atoi(argv[i]);
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }

    // Seed random
    srand(time(NULL));

    // Allocate clients
    client_t* clients = calloc(total, sizeof(client_t));
    if (!clients) {
        perror("calloc");
        return 1;
    }

    // Resolve hostname
    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", port);

    int ret = getaddrinfo(ip, port_str, &hints, &res);
    if (ret != 0) {
        fprintf(stderr, "Failed to resolve %s: %s\n", ip, gai_strerror(ret));
        free(clients);
        return 1;
    }

    struct sockaddr_in addr;
    memcpy(&addr, res->ai_addr, sizeof(addr));
    freeaddrinfo(res);

    // If no backends specified, test all
    if (!test_weather && !test_cities && !test_surprise) {
        test_weather = test_cities = test_surprise = 1;
    }

    // Create list of enabled backends
    int enabled_backends[3] = {0};
    int num_enabled = 0;
    if (test_weather)
        enabled_backends[num_enabled++] = 0; // Weather
    if (test_cities)
        enabled_backends[num_enabled++] = 1; // Cities
    if (test_surprise)
        enabled_backends[num_enabled++] = 2; // Surprise

    // Print configuration
    const char* mode_names[] = {"SLOW", "NORMAL", "FAST", "VERY FAST", "INSANE", "BURST", "CUSTOM"};
    printf("=== Enhanced REST API Stress Test ===\n");
    printf("Target:   %s:%d\n", ip, port);
    printf("Requests: %d\n", total);
    printf("Mode:     %s", mode_names[mode]);
    if (mode != MODE_BURST) {
        printf(" (%.0f req/sec)\n", 1000000.0 / interval_us);
    } else {
        printf(" (instant)\n");
    }
    if (realistic_timing) {
        printf("Timing:   Realistic (random 100-500ms think time)\n");
    }
    printf("Backends: ");
    if (test_weather)
        printf("Weather ");
    if (test_cities)
        printf("Cities ");
    if (test_surprise)
        printf("Surprise ");
    printf("\n");
    printf("=====================================\n\n");

    // Initialize clients
    for (int i = 0; i < total; i++) {
        clients[i].fd = -1;
        clients[i].state = CLIENT_CREATED;
        clients[i].request_type = enabled_backends[rand() % num_enabled];
        clients[i].city_index = rand() % NUM_CITIES; // Random city for weather requests

        // Generate request data
        if (clients[i].request_type == 0) { // Weather request
            const city_t* city = &CITIES[clients[i].city_index];
            char request_buf[512];
            snprintf(request_buf, sizeof(request_buf),
                     "GET /GetWeather?lat=%.6f&lon=%.6f HTTP/1.1\r\n"
                     "Host: localhost\r\n"
                     "User-Agent: StressTest/1.0\r\n"
                     "Accept: application/json\r\n"
                     "Connection: close\r\n\r\n",
                     city->latitude, city->longitude);
            clients[i].request_data = strdup(request_buf);
        } else {
            clients[i].request_data = strdup(REQUEST_TEMPLATES[clients[i].request_type]);
        }

        if (!clients[i].request_data) {
            fprintf(stderr, "Failed to allocate request data for client %d\n", i);
            continue;
        }

        clients[i].request_size = strlen(clients[i].request_data);
        clients[i].sent_bytes = 0;
        clients[i].response_buffer = malloc(MAX_RESPONSE_SIZE);
        clients[i].response_capacity = MAX_RESPONSE_SIZE;
        clients[i].response_bytes = 0;
        clients[i].http_status = 0;
        clients[i].think_time_ms = realistic_timing ? (rand() % 400 + 100) : 0;

        if (!clients[i].response_buffer) {
            fprintf(stderr, "Failed to allocate response buffer for client %d\n", i);
            // Continue anyway
        }

        clock_gettime(CLOCK_MONOTONIC, &clients[i].create_time);
    }

    printf("Starting stress test...\n");
    long long start_time_us = get_time_us();
    long long last_create_time_us = start_time_us;

    int next_to_create = 0;
    int active_count = 0;
    int completed_count = 0;
    int failed_count = 0;

    // Main event loop
    while (completed_count + failed_count < total) {
        long long now_us = get_time_us();

        // Create new clients
        int can_create = (next_to_create < total);
        if (mode == MODE_BURST) {
            can_create = can_create && (next_to_create == 0 || active_count < total);
        } else {
            can_create = can_create && ((now_us - last_create_time_us) >= interval_us);
        }

        if (can_create) {
            int fd = socket(AF_INET, SOCK_STREAM, 0);
            if (fd < 0) {
                clients[next_to_create].state = CLIENT_FAILED;
                failed_count++;
                next_to_create++;
            } else {
                // Set non-blocking
                int flags = fcntl(fd, F_GETFL, 0);
                fcntl(fd, F_SETFL, flags | O_NONBLOCK);

                clients[next_to_create].fd = fd;
                clients[next_to_create].state = CLIENT_CONNECTING;
                clock_gettime(CLOCK_MONOTONIC, &clients[next_to_create].connect_start);

                // Initiate connection
                int r = connect(fd, (struct sockaddr*)&addr, sizeof(addr));
                if (r == 0) {
                    // Connected immediately (unlikely)
                    clients[next_to_create].state = CLIENT_CONNECTED;
                    clock_gettime(CLOCK_MONOTONIC, &clients[next_to_create].connect_end);
                } else if (errno != EINPROGRESS) {
                    close(fd);
                    clients[next_to_create].state = CLIENT_FAILED;
                    failed_count++;
                }

                active_count++;
                next_to_create++;
            }

            last_create_time_us = now_us;

            if (mode == MODE_BURST && next_to_create < total) {
                continue;
            }
        }

        // Process active clients
        fd_set readfds, writefds, errfds;
        FD_ZERO(&readfds);
        FD_ZERO(&writefds);
        FD_ZERO(&errfds);
        int maxfd = 0;

        struct timespec now_ts;
        clock_gettime(CLOCK_MONOTONIC, &now_ts);

        for (int i = 0; i < next_to_create; i++) {
            if (clients[i].state == CLIENT_DONE || clients[i].state == CLIENT_FAILED) {
                continue;
            }

            int fd = clients[i].fd;
            if (fd < 0)
                continue;

            // Timeout checks
            long long elapsed_us = timespec_diff_us(&clients[i].connect_start, &now_ts);
            if (elapsed_us > RESPONSE_TIMEOUT_SEC * 1000000LL) {
                close(fd);
                clients[i].state = CLIENT_FAILED;
                active_count--;
                failed_count++;
                continue;
            }

            switch (clients[i].state) {
            case CLIENT_CONNECTING:
                FD_SET(fd, &writefds);
                FD_SET(fd, &errfds);
                break;
            case CLIENT_CONNECTED:
                // Check if think time elapsed
                if (clients[i].think_time_ms > 0) {
                    long long think_elapsed = timespec_diff_us(&clients[i].connect_end, &now_ts) / 1000;
                    if (think_elapsed < clients[i].think_time_ms) {
                        break; // Still thinking
                    }
                }
                // Ready to send
                clients[i].state = CLIENT_SENDING;
                clock_gettime(CLOCK_MONOTONIC, &clients[i].send_start);
                // Fall through
            case CLIENT_SENDING:
                FD_SET(fd, &writefds);
                break;
            case CLIENT_SENT:
            case CLIENT_RECEIVING:
                FD_SET(fd, &readfds);
                break;
            default:
                break;
            }

            if (fd > maxfd)
                maxfd = fd;
        }

        if (maxfd == 0) {
            usleep(100);
            continue;
        }

        // Select with short timeout
        struct timeval tv = {0, 1000}; // 1ms
        int n = select(maxfd + 1, &readfds, &writefds, &errfds, &tv);

        if (n < 0) {
            if (errno == EINTR)
                continue;
            perror("select");
            break;
        }

        if (n == 0)
            continue;

        // Process ready sockets
        for (int i = 0; i < next_to_create && n > 0; i++) {
            int fd = clients[i].fd;
            if (fd < 0)
                continue;

            // Check for errors
            if (FD_ISSET(fd, &errfds)) {
                close(fd);
                clients[i].state = CLIENT_FAILED;
                active_count--;
                failed_count++;
                n--;
                continue;
            }

            // Handle state transitions
            if (clients[i].state == CLIENT_CONNECTING && FD_ISSET(fd, &writefds)) {
                // Check if connection succeeded
                int error = 0;
                socklen_t len = sizeof(error);
                if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &len) < 0 || error != 0) {
                    close(fd);
                    clients[i].state = CLIENT_FAILED;
                    active_count--;
                    failed_count++;
                } else {
                    clients[i].state = CLIENT_CONNECTED;
                    clock_gettime(CLOCK_MONOTONIC, &clients[i].connect_end);
                    clients[i].connect_time_us = timespec_diff_us(&clients[i].connect_start, &clients[i].connect_end);
                }
                n--;
            } else if (clients[i].state == CLIENT_SENDING && FD_ISSET(fd, &writefds)) {
                // Send data
                ssize_t sent = send(fd, clients[i].request_data + clients[i].sent_bytes,
                                    clients[i].request_size - clients[i].sent_bytes, MSG_DONTWAIT);
                if (sent < 0) {
                    if (errno != EAGAIN && errno != EWOULDBLOCK) {
                        close(fd);
                        clients[i].state = CLIENT_FAILED;
                        active_count--;
                        failed_count++;
                    }
                } else {
                    clients[i].sent_bytes += sent;
                    if (clients[i].sent_bytes >= clients[i].request_size) {
                        clock_gettime(CLOCK_MONOTONIC, &clients[i].send_end);
                        clients[i].send_time_us = timespec_diff_us(&clients[i].send_start, &clients[i].send_end);
                        clients[i].state = CLIENT_SENT;
                        clock_gettime(CLOCK_MONOTONIC, &clients[i].recv_start);
                    }
                }
                n--;
            } else if ((clients[i].state == CLIENT_SENT || clients[i].state == CLIENT_RECEIVING) &&
                       FD_ISSET(fd, &readfds)) {
                // Receive response
                if (clients[i].response_bytes < clients[i].response_capacity) {
                    ssize_t received = recv(fd, clients[i].response_buffer + clients[i].response_bytes,
                                            clients[i].response_capacity - clients[i].response_bytes - 1, 0);
                    if (received < 0) {
                        if (errno != EAGAIN && errno != EWOULDBLOCK) {
                            close(fd);
                            clients[i].state = CLIENT_FAILED;
                            active_count--;
                            failed_count++;
                        }
                    } else if (received == 0) {
                        // Connection closed
                        clock_gettime(CLOCK_MONOTONIC, &clients[i].recv_end);
                        clients[i].response_buffer[clients[i].response_bytes] = '\0';
                        clients[i].http_status =
                            parse_http_status(clients[i].response_buffer, clients[i].response_bytes);
                        clients[i].response_time_us = timespec_diff_us(&clients[i].recv_start, &clients[i].recv_end);
                        clients[i].total_time_us = timespec_diff_us(&clients[i].connect_start, &clients[i].recv_end);
                        close(fd);
                        clients[i].state = CLIENT_DONE;
                        active_count--;
                        completed_count++;
                    } else {
                        clients[i].response_bytes += received;
                        clients[i].state = CLIENT_RECEIVING;
                    }
                }
                n--;
            }
        }

        // Progress update (single line with \r)
        long long elapsed = get_time_us() - start_time_us;
        double rate = (completed_count + failed_count) * 1000000.0 / elapsed;
        double percent = 100.0 * (completed_count + failed_count) / total;

        // Get terminal width and calculate bar width (leave room for text)
        int term_width = get_terminal_width();
        int bar_width = term_width - 50; // Reserve ~50 chars for text
        if (bar_width < 10)
            bar_width = 10; // Minimum bar width
        if (bar_width > 50)
            bar_width = 50; // Maximum bar width

        int filled = (int)(bar_width * (completed_count + failed_count) / total);
        printf("\r[");
        for (int i = 0; i < bar_width; i++) {
            if (i < filled)
                printf("=");
            else if (i == filled)
                printf(">");
            else
                printf(" ");
        }
        printf("] %3.0f%% | %d/%d | %.0f req/s", percent, completed_count + failed_count, total, rate);
        fflush(stdout);
    }

    // Clear progress line and move to next line
    printf("\n");

    long long end_time_us = get_time_us();
    double elapsed_sec = (end_time_us - start_time_us) / 1000000.0;

    // Calculate statistics
    long long* connect_times = malloc(completed_count * sizeof(long long));
    long long* response_times = malloc(completed_count * sizeof(long long));
    long long* total_times = malloc(completed_count * sizeof(long long));

    // Track individual status codes (up to 100 unique codes)
    int unique_status_codes[100] = {0};
    int unique_status_counts[100] = {0};
    int num_unique_codes = 0;

    int metric_idx = 0;
    for (int i = 0; i < total; i++) {
        if (clients[i].state == CLIENT_DONE) {
            if (metric_idx < completed_count) {
                connect_times[metric_idx] = clients[i].connect_time_us;
                response_times[metric_idx] = clients[i].response_time_us;
                total_times[metric_idx] = clients[i].total_time_us;
                metric_idx++;
            }

            // Track status code
            int status = clients[i].http_status;
            int found = 0;
            for (int j = 0; j < num_unique_codes; j++) {
                if (unique_status_codes[j] == status) {
                    unique_status_counts[j]++;
                    found = 1;
                    break;
                }
            }
            if (!found && num_unique_codes < 100) {
                unique_status_codes[num_unique_codes] = status;
                unique_status_counts[num_unique_codes] = 1;
                num_unique_codes++;
            }
        }
    }

    // Sort for percentiles
    qsort(connect_times, completed_count, sizeof(long long), compare_long_long);
    qsort(response_times, completed_count, sizeof(long long), compare_long_long);
    qsort(total_times, completed_count, sizeof(long long), compare_long_long);

    // Print results
    printf("\n=== Results ===\n");
    printf("Total requests:   %d\n", total);
    printf("Completed:        %d (%.1f%%)\n", completed_count, 100.0 * completed_count / total);
    printf("Failed:           %d (%.1f%%)\n", failed_count, 100.0 * failed_count / total);
    printf("Time elapsed:     %.3f seconds\n", elapsed_sec);
    printf("Throughput:       %.0f req/sec\n", completed_count / elapsed_sec);

    printf("\nHTTP Status Codes:\n");
    if (num_unique_codes > 0) {
        // Sort status codes for display
        for (int i = 0; i < num_unique_codes - 1; i++) {
            for (int j = i + 1; j < num_unique_codes; j++) {
                if (unique_status_codes[i] > unique_status_codes[j]) {
                    int temp_code = unique_status_codes[i];
                    int temp_count = unique_status_counts[i];
                    unique_status_codes[i] = unique_status_codes[j];
                    unique_status_counts[i] = unique_status_counts[j];
                    unique_status_codes[j] = temp_code;
                    unique_status_counts[j] = temp_count;
                }
            }
        }

        for (int i = 0; i < num_unique_codes; i++) {
            const char* description = "";
            int code = unique_status_codes[i];

            if (code == 0)
                description = "No Response";
            else if (code == 200)
                description = "OK";
            else if (code == 201)
                description = "Created";
            else if (code == 204)
                description = "No Content";
            else if (code == 301)
                description = "Moved Permanently";
            else if (code == 302)
                description = "Found";
            else if (code == 304)
                description = "Not Modified";
            else if (code == 400)
                description = "Bad Request";
            else if (code == 401)
                description = "Unauthorized";
            else if (code == 403)
                description = "Forbidden";
            else if (code == 404)
                description = "Not Found";
            else if (code == 413)
                description = "Content Too Large";
            else if (code == 500)
                description = "Internal Server Error";
            else if (code == 501)
                description = "Not Implemented";
            else if (code == 502)
                description = "Bad Gateway";
            else if (code == 503)
                description = "Service Unavailable";

            printf("  %6d  %3d  %-25s\n", unique_status_counts[i], code, description);
        }
    } else {
        printf("  No responses received\n");
    }

    if (completed_count > 0) {
        printf("\nLatency Metrics (milliseconds):\n");
        printf("                   p50      p95      p99      max\n");
        printf("  Connect:    %7.2f  %7.2f  %7.2f  %7.2f\n",
               calculate_percentile(connect_times, completed_count, 50) / 1000.0,
               calculate_percentile(connect_times, completed_count, 95) / 1000.0,
               calculate_percentile(connect_times, completed_count, 99) / 1000.0,
               connect_times[completed_count - 1] / 1000.0);
        printf("  Response:   %7.2f  %7.2f  %7.2f  %7.2f\n",
               calculate_percentile(response_times, completed_count, 50) / 1000.0,
               calculate_percentile(response_times, completed_count, 95) / 1000.0,
               calculate_percentile(response_times, completed_count, 99) / 1000.0,
               response_times[completed_count - 1] / 1000.0);
        printf("  Total:      %7.2f  %7.2f  %7.2f  %7.2f\n",
               calculate_percentile(total_times, completed_count, 50) / 1000.0,
               calculate_percentile(total_times, completed_count, 95) / 1000.0,
               calculate_percentile(total_times, completed_count, 99) / 1000.0,
               total_times[completed_count - 1] / 1000.0);
    }

    // Print responses if count is 10 or less
    if (total <= 10) {
        printf("\n=== Response Data ===\n");
        for (int i = 0; i < total; i++) {
            if (clients[i].state == CLIENT_DONE && clients[i].response_buffer) {
                printf("\n--- Request #%d (Status: %d, %zu bytes) ---\n", i + 1, clients[i].http_status,
                       clients[i].response_bytes);
                printf("%s\n", clients[i].response_buffer);
            } else if (clients[i].state == CLIENT_FAILED) {
                printf("\n--- Request #%d (FAILED) ---\n", i + 1);
            }
        }
    }

    // Keepalive
    if (keepalive_sec > 0) {
        printf("\nKeeping connections alive for %d seconds...\n", keepalive_sec);
        sleep(keepalive_sec);
    }

    // Cleanup
    for (int i = 0; i < total; i++) {
        if (clients[i].fd >= 0) {
            shutdown(clients[i].fd, SHUT_RDWR);
            close(clients[i].fd);
        }
        if (clients[i].response_buffer) {
            free(clients[i].response_buffer);
        }
        if (clients[i].request_data) {
            free(clients[i].request_data);
        }
    }

    free(clients);
    free(connect_times);
    free(response_times);
    free(total_times);

    printf("\nDone!\n");
    return (completed_count == total) ? 0 : 1;
}

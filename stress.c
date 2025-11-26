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
#include <signal.h>
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

// Stop flag for eternal mode, set by SIGINT handler
volatile sig_atomic_t stop_requested = 0;
static void sigint_handler(int signum) {
    (void)signum;
    stop_requested = 1;
}

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

static const city_t CITIES[] = {
    // 🇸🇪 Top 10 Major Metropolitan Areas
    {"Stockholm", 59.3293, 18.0686},
    {"Göteborg", 57.7089, 11.9746},
    {"Malmö", 55.6049, 13.0038},
    {"Uppsala", 59.8586, 17.6389},
    {"Västerås", 59.6099, 16.5448},
    {"Örebro", 59.2741, 15.2066},
    {"Linköping", 58.4108, 15.6214},
    {"Helsingborg", 56.0465, 12.6944},
    {"Jönköping", 57.7826, 14.1618},
    {"Norrköping", 58.5877, 16.1924},

    // 🏙️ Cities ranked 11 to 20
    {"Lund", 55.7047, 13.1910},
    {"Umeå", 63.8258, 20.2630},
    {"Gävle", 60.6745, 17.1417},
    {"Borås", 57.7210, 12.9401},
    {"Eskilstuna", 59.3712, 16.5098},
    {"Södertälje", 59.1955, 17.6253},
    {"Karlstad", 59.3793, 13.5036},
    {"Täby", 59.4000, 18.0667},
    {"Växjö", 56.8790, 14.8059},
    {"Sundsvall", 62.3908, 17.3069},

    // 🏘️ Cities ranked 21 to 30
    {"Halmstad", 56.6745, 12.8571},
    {"Luleå", 65.5848, 22.1567},
    {"Trollhättan", 58.2837, 12.2886},
    {"Östersund", 63.1767, 14.6361},
    {"Borlänge", 60.4855, 15.4385},
    {"Tumba", 59.2000, 17.8333},
    {"Skövde", 58.3912, 13.8451},
    {"Kalmar", 56.6634, 16.3568},
    {"Kristianstad", 56.0313, 14.1524},
    {"Falun", 60.6036, 15.6259},

    // 🏘️ Cities ranked 31 to 40
    {"Karlskrona", 56.1608, 15.5866},
    {"Skellefteå", 64.7500, 20.9500},
    {"Uddevalla", 58.3498, 11.9356},
    {"Nyköping", 58.7535, 17.0019},
    {"Varberg", 57.1054, 12.2519},
    {"Motala", 58.5398, 15.0381},
    {"Landskrona", 55.8670, 12.8300},
    {"Köping", 59.5100, 16.0000},
    {"Arvika", 59.6558, 12.5857},
    {"Piteå", 65.3167, 21.4667},

    // 🏘️ Cities ranked 41 to 50
    {"Huddinge", 59.2333, 17.9833},
    {"Ängelholm", 56.2307, 12.8687},
    {"Alingsås", 57.9292, 12.5298},
    {"Kiruna", 67.8557, 20.2253},
    {"Visby", 57.6333, 18.3000},
    {"Värnamo", 56.8732, 14.0436},
    {"Katrineholm", 59.0022, 16.2081},
    {"Kungälv", 57.8596, 11.9861},
    {"Västervik", 57.7500, 16.6333},
    {"Trelleborg", 55.3750, 13.1500},

    // 🏘️ Cities ranked 51 to 60
    {"Mjölby", 58.3242, 15.1325},
    {"Sandviken", 60.6214, 16.7820},
    {"Oskarshamn", 57.2917, 16.4500},
    {"Härnösand", 62.6322, 17.9405},
    {"Lidköping", 58.5000, 13.1667},
    {"Karlshamn", 56.1694, 14.8688},
    {"Falkenberg", 56.9080, 12.4939},
    {"Boo", 59.3333, 18.2500},
    {"Hässleholm", 56.1667, 13.7833},
    {"Ystad", 55.4287, 13.8202},

    // 🏘️ Cities ranked 61 to 70
    {"Eslöv", 55.8333, 13.3000},
    {"Norrtälje", 59.7600, 18.7000},
    {"Enköping", 59.6333, 17.1000},
    {"Vänersborg", 58.3667, 12.3167},
    {"Boden", 65.8250, 21.6889},
    {"Kumla", 59.1333, 15.1333},
    {"Kungsbacka", 57.4833, 12.0833},
    {"Nässjö", 57.6500, 14.4833},
    {"Vetlanda", 57.6833, 15.0500},
    {"Simrishamn", 55.5500, 14.3500},

    // 🏘️ Cities ranked 71 to 80
    {"Falköping", 58.1708, 13.5417},
    {"Ljungby", 56.8333, 13.9333},
    {"Kristinehamn", 59.3000, 14.1000},
    {"Mariestad", 58.7167, 13.8167},
    {"Strängnäs", 59.3789, 17.0267},
    {"Säffle", 59.1239, 12.9234},
    {"Habo", 57.9000, 14.0500},
    {"Bålsta", 59.5833, 17.5333},
    {"Avesta", 60.1420, 16.1691},
    {"Flen", 59.0500, 16.5833},

    // 🏘️ Cities ranked 81 to 90
    {"Hultsfred", 57.4858, 15.8344},
    {"Bjuv", 56.0964, 13.0642},
    {"Ludvika", 60.1500, 15.1833},
    {"Söderhamn", 61.3000, 17.0667},
    {"Sala", 59.9167, 16.6000},
    {"Vaxholm", 59.4011, 18.3589},
    {"Ronneby", 56.2000, 15.2833},
    {"Klippan", 56.1333, 13.1333},
    {"Staffanstorp", 55.6333, 13.2000},
    {"Torshälla", 59.4167, 16.4833},

    // 🏘️ Cities ranked 91 to 100
    {"Älmhult", 56.5670, 14.1370},
    {"Timrå", 62.4833, 17.3333},
    {"Vellinge", 55.4500, 13.0333},
    {"Nybro", 56.6850, 15.9189},
    {"Laholm", 56.5167, 13.0500},
    {"Finspång", 58.7000, 15.7167},
    {"Olofström", 56.2730, 14.5372},
    {"Hörby", 55.8500, 13.6333},
    {"Gnesta", 59.0500, 17.3000},
    {"Hultsfred", 57.4858, 15.8344}};

#define NUM_CITIES (sizeof(CITIES) / sizeof(CITIES[0]))

// Request templates for backend testing
static const char* REQUEST_TEMPLATES[] = {
    NULL, // Weather template - dynamically generated
    "GET /cities HTTP/1.1\r\n"
    "Host: localhost\r\n"
    "User-Agent: StressTest/1.0\r\n"
    "Accept: application/json\r\n"
    "Connection: close\r\n\r\n",

    "GET /surprise HTTP/1.1\r\n"
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
    MODE_VERY_SLOW, // 1000ms interval (1 req/sec)
    MODE_SLOW,      // 250ms interval (~4 req/sec)
    MODE_NORMAL,    // 50ms interval (~20 req/sec)
    MODE_FAST,      // 1ms interval (~1000 req/sec)
    MODE_BURST,     // No delay
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

// URL-encode a string. Caller must free the returned pointer.
static char* url_encode(const char* s) {
    if (!s)
        return NULL;
    size_t len = strlen(s);
    // Worst case every char is encoded as %XX -> 3x
    char* out = malloc(len * 3 + 1);
    if (!out)
        return NULL;
    char* o = out;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)s[i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' ||
            c == '.' || c == '~') {
            *o++ = c;
        } else if (c == ' ') {
            *o++ = '+'; // spaces -> plus
        } else {
            // percent-encode
            static const char hex[] = "0123456789ABCDEF";
            *o++ = '%';
            *o++ = hex[c >> 4];
            *o++ = hex[c & 0xF];
        }
    }
    *o = '\0';
    return out;
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
    printf("  -very-slow 1000ms interval (~1 req/sec)\n");
    printf("  -slow      250ms interval (~4 req/sec)\n");
    printf("  -normal    50ms interval (~20 req/sec)\n");
    printf("  -fast      1ms interval (~1000 req/sec)\n");
    printf("  -burst     No delay (all requests at once)\n");
    printf("  -custom <us> Custom interval in microseconds\n");
    printf("              [DEFAULT: -slow, 250ms intervals (~4 req/sec)]\n\n");
    printf("Backend Selection:\n");
    printf("  -weather        Test weather backend (cycles through major Swedish cities)\n");
    printf("  -cities         Test cities backend (/cities)\n");
    printf("  -surprise       Test surprise backend (/surprise)\n");
    printf("                  [DEFAULT: test all backends if none specified]\n\n");
    printf("Options:\n");
    printf("  -ip <addr>      Server IP or hostname (default: %s)\n", DEFAULT_IP);
    printf("  -port <num>     Server port (default: %d)\n", DEFAULT_PORT);
    printf("  -count <num>    Number of requests (default: %d)\n", DEFAULT_CONN);
    printf("  -count eternal  Run forever until interrupted (uses concurrency=%d)\n", DEFAULT_CONN);
    printf("  -realistic      Add random think time (100-500ms) after connection\n");
    printf("  -msg <path>     Use custom request path (e.g. \"/weather?city=oslo\")\n");
    printf("  -nr <N>         Concurrency for eternal runs (default: %d)\n", DEFAULT_CONN);
    printf("  -keepalive <s>  Keep connections open for N seconds (default: 0)\n");
    printf("  -h, -help       Show this help\n\n");
    printf("Control:\n");
    printf("  Ctrl-C (SIGINT) stops an eternal run and prints summary\n\n");
    printf("Examples:\n");
    printf("  %s -count 100 -weather                    # Test weather backend with trickle\n", prog);
    printf("  %s -count 50 -cities -surprise            # Test cities and surprise backends\n", prog);
    printf("  %s -fast -weather -cities -surprise       # Fast test of all backends\n", prog);
    printf("  %s -burst -count 1000 -realistic          # Burst test with think time\n", prog);
    printf("  %s -custom 500000 -count 20 -surprise     # Custom 500ms intervals\n", prog);
    printf("  %s -count eternal -fast                  # Run forever until interrupted\n", prog);
}

int main(int argc, char** argv) {
    const char* ip = DEFAULT_IP;
    int port = DEFAULT_PORT;
    int total = DEFAULT_CONN;
    int concurrency = DEFAULT_CONN; // concurrency for eternal runs (set with -nr)
    speed_mode_t mode = MODE_SLOW;  // Default to slow (250ms) mode
    int interval_us = 250000;       // 250ms default slow rate
    int realistic_timing = 0;
    const char* msg_path = NULL;
    int keepalive_sec = 0;
    int log_to_file = 0;
    FILE* log_file = NULL;
    char log_filename[256] = {0};

    // Eternal/run-forever support
    int eternal = 0; // set when -count eternal
    long long requests_sent = 0;

    // Backend selection flags
    int test_weather = 0;
    int test_cities = 0;
    int test_surprise = 0;

    // Parse arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "-help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "-very-slow") == 0) {
            mode = MODE_VERY_SLOW;
            interval_us = 1000000; // 1000ms (1s)
        } else if (strcmp(argv[i], "-slow") == 0) {
            mode = MODE_SLOW;
            interval_us = 250000; // 250ms
        } else if (strcmp(argv[i], "-normal") == 0) {
            mode = MODE_NORMAL;
            interval_us = 50000; // 50ms
        } else if (strcmp(argv[i], "-fast") == 0) {
            mode = MODE_FAST;
            interval_us = 1000; // 1ms
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
            if (strcmp(argv[i], "eternal") == 0) {
                eternal = 1;
                // use DEFAULT_CONN as concurrency for eternal mode
                /* total remains DEFAULT_CONN for compatibility but we'll
                   allocate using client_capacity below */
            } else {
                total = atoi(argv[i]);
                if (total <= 0) {
                    fprintf(stderr, "Error: -count must be > 0 or 'eternal'\n");
                    return 1;
                }
            }
        } else if (strcmp(argv[i], "-realistic") == 0) {
            realistic_timing = 1;
        } else if (strcmp(argv[i], "-nr") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "Error: -nr requires an argument\n");
                return 1;
            }
            concurrency = atoi(argv[i]);
            if (concurrency <= 0) {
                fprintf(stderr, "Error: -nr must be > 0\n");
                return 1;
            }
        } else if (strcmp(argv[i], "-msg") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "Error: -msg requires an argument\n");
                return 1;
            }
            msg_path = argv[i];
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
        } else if (strcmp(argv[i], "-log") == 0) {
            log_to_file = 1;
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }

    // Seed random
    srand(time(NULL));

    // Install SIGINT handler so eternal runs can be stopped with Ctrl-C
    signal(SIGINT, sigint_handler);

    // Determine client buffer size. For eternal mode use `concurrency` (-nr), otherwise `total`.
    int client_capacity = eternal ? concurrency : total;

    // Allocate clients
    client_t* clients = calloc(client_capacity, sizeof(client_t));
    if (!clients) {
        perror("calloc");
        return 1;
    }

    // Normalize `ip` argument: allow URLs like "http://host[:port]/..." and extract hostname
    char host_only[256] = {0};
    const char* src = ip;
    if (strncmp(src, "http://", 7) == 0) {
        src += 7;
    } else if (strncmp(src, "https://", 8) == 0) {
        src += 8;
    }

    // Extract hostname (stop at ':' or '/' or end)
    const char* p = src;
    size_t hi = 0;
    while (*p && *p != '/' && *p != ':' && hi + 1 < sizeof(host_only)) {
        host_only[hi++] = *p++;
    }
    host_only[hi] = '\0';

    // If user included a port in the URL (e.g. host:8080), and -port was not explicitly set,
    // update `port` to use that value. We only override when current `port` equals DEFAULT_PORT.
    if (*p == ':') {
        p++;
        char port_part[16] = {0};
        size_t pj = 0;
        while (*p && *p != '/' && pj + 1 < sizeof(port_part)) {
            port_part[pj++] = *p++;
        }
        port_part[pj] = '\0';
        if (port == DEFAULT_PORT && pj > 0) {
            int parsed = atoi(port_part);
            if (parsed > 0) {
                port = parsed;
            }
        }
    }

    // If extraction failed, fall back to the original `ip` value
    const char* resolve_name = (*host_only) ? host_only : ip;

    // Resolve hostname
    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", port);

    int ret = getaddrinfo(resolve_name, port_str, &hints, &res);
    if (ret != 0) {
        fprintf(stderr, "Failed to resolve %s: %s\n", resolve_name, gai_strerror(ret));
        free(clients);
        return 1;
    }

    struct sockaddr_in addr;
    memcpy(&addr, res->ai_addr, sizeof(addr));
    freeaddrinfo(res);

    // Build a Host header value from the resolved hostname and port
    char host_header_global[256];
    if (port == 80) {
        snprintf(host_header_global, sizeof(host_header_global), "%s", resolve_name);
    } else {
        snprintf(host_header_global, sizeof(host_header_global), "%s:%d", resolve_name, port);
    }

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

    // Create log file if requested
    if (log_to_file) {
        time_t now = time(NULL);
        struct tm* tm_info = localtime(&now);
        snprintf(log_filename, sizeof(log_filename), "stress_test_%04d%02d%02d_%02d%02d%02d.log",
                 tm_info->tm_year + 1900, tm_info->tm_mon + 1, tm_info->tm_mday, tm_info->tm_hour, tm_info->tm_min,
                 tm_info->tm_sec);
        log_file = fopen(log_filename, "w");
        if (!log_file) {
            fprintf(stderr, "Warning: Failed to create log file %s: %s\n", log_filename, strerror(errno));
            log_to_file = 0;
        } else {
            printf("Logging to: %s\n", log_filename);
        }
    }

    // Print configuration
    const char* mode_names[] = {"VERY SLOW", "SLOW", "NORMAL", "FAST", "BURST", "CUSTOM"};
    printf("=== Enhanced REST API Stress Test ===\n");
    printf("Target:   %s:%d\n", ip, port);
    if (eternal)
        printf("Requests: eternal (concurrency %d)\n", client_capacity);
    else
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

    // Write configuration to log file
    if (log_file) {
        fprintf(log_file, "=== Enhanced REST API Stress Test ===\n");
        fprintf(log_file, "Target:   %s:%d\n", ip, port);
        fprintf(log_file, "Requests: %d\n", total);
        fprintf(log_file, "Mode:     %s", mode_names[mode]);
        if (mode != MODE_BURST) {
            fprintf(log_file, " (%.0f req/sec)\n", 1000000.0 / interval_us);
        } else {
            fprintf(log_file, " (instant)\n");
        }
        if (realistic_timing) {
            fprintf(log_file, "Timing:   Realistic (random 100-500ms think time)\n");
        }
        fprintf(log_file, "Backends: ");
        if (test_weather)
            fprintf(log_file, "Weather ");
        if (test_cities)
            fprintf(log_file, "Cities ");
        if (test_surprise)
            fprintf(log_file, "Surprise ");
        fprintf(log_file, "\n");
        fprintf(log_file, "=====================================\n\n");
        fflush(log_file);
    }

    // Initialize clients
    for (int i = 0; i < client_capacity; i++) {
        clients[i].fd = -1;
        clients[i].state = CLIENT_CREATED;
        clients[i].request_type = enabled_backends[rand() % num_enabled];
        clients[i].city_index = i % NUM_CITIES; // Cycle through cities sequentially

        // Generate request data
        if (msg_path) {
            // Use provided custom path for every request
            const char* path = msg_path;
            char full_path[512];
            if (path[0] != '/') {
                snprintf(full_path, sizeof(full_path), "/%s", path);
            } else {
                strncpy(full_path, path, sizeof(full_path) - 1);
                full_path[sizeof(full_path) - 1] = '\0';
            }
            char request_buf[1024];
            snprintf(request_buf, sizeof(request_buf),
                     "GET %s HTTP/1.1\r\n"
                     "Host: %s\r\n"
                     "User-Agent: StressTest/1.0\r\n"
                     "Accept: */*\r\n"
                     "Connection: close\r\n\r\n",
                     full_path, host_header_global);
            clients[i].request_data = strdup(request_buf);
        } else if (clients[i].request_type == 0) { // Weather request
            const city_t* city = &CITIES[clients[i].city_index];
            // Encode city name for URL
            char* enc = url_encode(city->name);
            char path[512];
            if (enc) {
                snprintf(path, sizeof(path), "/weather?city=%s", enc);
            } else {
                // fallback: raw name (may break URL)
                snprintf(path, sizeof(path), "/weather?city=%s", city->name);
            }
            char request_buf[1024];
            snprintf(request_buf, sizeof(request_buf),
                     "GET %s HTTP/1.1\r\n"
                     "Host: %s\r\n"
                     "User-Agent: StressTest/1.0\r\n"
                     "Accept: application/json\r\n"
                     "Connection: close\r\n\r\n",
                     path, host_header_global);
            clients[i].request_data = strdup(request_buf);
            if (enc)
                free(enc);
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
    while ((eternal && !stop_requested) || (!eternal && (completed_count + failed_count < total))) {
        long long now_us = get_time_us();

        // Create new clients
        int can_create = 0;
        if (!eternal) {
            can_create = (next_to_create < total);
            if (mode == MODE_BURST) {
                can_create = can_create && (next_to_create == 0 || active_count < total);
            } else {
                can_create = can_create && ((now_us - last_create_time_us) >= interval_us);
            }
        } else {
            // Eternal mode: create when we have capacity and interval elapsed
            if (mode == MODE_BURST) {
                can_create = (active_count < client_capacity);
            } else {
                can_create = (active_count < client_capacity) && ((now_us - last_create_time_us) >= interval_us);
            }
        }

        if (can_create) {
            int idx = next_to_create % client_capacity;
            if (eternal) {
                // find a free slot to reuse
                int found = 0;
                for (int s = 0; s < client_capacity; s++) {
                    int cand = (idx + s) % client_capacity;
                    if (clients[cand].state == CLIENT_CREATED || clients[cand].state == CLIENT_DONE ||
                        clients[cand].state == CLIENT_FAILED || clients[cand].fd < 0) {
                        idx = cand;
                        found = 1;
                        break;
                    }
                }
                if (!found) {
                    // no free slot now
                    last_create_time_us = now_us;
                    // skip creating this tick
                    // fall through to processing sockets
                }
            } else {
                idx = next_to_create;
            }

            if (idx >= 0 && idx < client_capacity) {
                // Clean up any previous buffers in reused slot
                if (clients[idx].response_buffer) {
                    free(clients[idx].response_buffer);
                    clients[idx].response_buffer = NULL;
                }
                if (clients[idx].request_data) {
                    free(clients[idx].request_data);
                    clients[idx].request_data = NULL;
                }

                clients[idx].fd = -1;
                clients[idx].state = CLIENT_CREATED;
                clients[idx].request_type = enabled_backends[rand() % num_enabled];
                clients[idx].city_index = idx % NUM_CITIES;
                clients[idx].response_capacity = MAX_RESPONSE_SIZE;
                clients[idx].response_buffer = malloc(MAX_RESPONSE_SIZE);
                clients[idx].response_bytes = 0;
                clients[idx].sent_bytes = 0;
                clients[idx].http_status = 0;

                // Generate request data for the slot
                if (msg_path) {
                    const char* path = msg_path;
                    char full_path[512];
                    if (path[0] != '/')
                        snprintf(full_path, sizeof(full_path), "/%s", path);
                    else {
                        strncpy(full_path, path, sizeof(full_path) - 1);
                        full_path[sizeof(full_path) - 1] = '\0';
                    }
                    char request_buf[1024];
                    snprintf(request_buf, sizeof(request_buf),
                             "GET %s HTTP/1.1\r\n"
                             "Host: %s\r\n"
                             "User-Agent: StressTest/1.0\r\n"
                             "Accept: */*\r\n"
                             "Connection: close\r\n\r\n",
                             full_path, host_header_global);
                    clients[idx].request_data = strdup(request_buf);
                } else if (clients[idx].request_type == 0) {
                    const city_t* city = &CITIES[clients[idx].city_index];
                    char* enc = url_encode(city->name);
                    char path[512];
                    if (enc) {
                        snprintf(path, sizeof(path), "/weather?city=%s", enc);
                        free(enc);
                    } else {
                        snprintf(path, sizeof(path), "/weather?city=%s", city->name);
                    }
                    char request_buf[1024];
                    snprintf(request_buf, sizeof(request_buf),
                             "GET %s HTTP/1.1\r\n"
                             "Host: %s\r\n"
                             "User-Agent: StressTest/1.0\r\n"
                             "Accept: application/json\r\n"
                             "Connection: close\r\n\r\n",
                             path, host_header_global);
                    clients[idx].request_data = strdup(request_buf);
                } else {
                    clients[idx].request_data = strdup(REQUEST_TEMPLATES[clients[idx].request_type]);
                }

                if (clients[idx].request_data)
                    clients[idx].request_size = strlen(clients[idx].request_data);

                clients[idx].think_time_ms = realistic_timing ? (rand() % 400 + 100) : 0;
                clock_gettime(CLOCK_MONOTONIC, &clients[idx].create_time);

                // Create socket and start connect
                int fd = socket(AF_INET, SOCK_STREAM, 0);
                if (fd < 0) {
                    clients[idx].state = CLIENT_FAILED;
                    failed_count++;
                } else {
                    int flags = fcntl(fd, F_GETFL, 0);
                    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
                    clients[idx].fd = fd;
                    clients[idx].state = CLIENT_CONNECTING;
                    clock_gettime(CLOCK_MONOTONIC, &clients[idx].connect_start);
                    int r = connect(fd, (struct sockaddr*)&addr, sizeof(addr));
                    if (r == 0) {
                        clients[idx].state = CLIENT_CONNECTED;
                        clock_gettime(CLOCK_MONOTONIC, &clients[idx].connect_end);
                    } else if (errno != EINPROGRESS) {
                        close(fd);
                        clients[idx].state = CLIENT_FAILED;
                        failed_count++;
                        if (total < 11) {
                            printf("\n--- Client #%d FAILED (connect) ---\n", idx + 1);
                            if (clients[idx].request_data)
                                printf("Request:\n%s\n", clients[idx].request_data);
                            if (clients[idx].response_buffer && clients[idx].response_bytes > 0) {
                                printf("Response:\n");
                                fwrite(clients[idx].response_buffer, 1, clients[idx].response_bytes, stdout);
                                printf("\n");
                            }
                            fflush(stdout);
                        }
                    } else {
                        active_count++;
                    }
                }

                next_to_create++;
                requests_sent++;
                last_create_time_us = now_us;
            }

            if (!eternal && mode == MODE_BURST && next_to_create < total) {
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

        int scan_limit = eternal ? client_capacity : next_to_create;
        for (int i = 0; i < scan_limit; i++) {
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
                // If we already have some bytes from the server, treat this as a completed
                // request (partial response) rather than a hard failure. Only count as
                // failed when no response bytes were received.
                if (clients[i].response_bytes > 0) {
                    clients[i].response_buffer[clients[i].response_bytes] = '\0';
                    clients[i].http_status = parse_http_status(clients[i].response_buffer, clients[i].response_bytes);
                    clock_gettime(CLOCK_MONOTONIC, &clients[i].recv_end);
                    clients[i].response_time_us = timespec_diff_us(&clients[i].recv_start, &clients[i].recv_end);
                    clients[i].total_time_us = timespec_diff_us(&clients[i].connect_start, &clients[i].recv_end);
                    clients[i].state = CLIENT_DONE;
                    active_count--;
                    completed_count++;
                } else {
                    clients[i].state = CLIENT_FAILED;
                    active_count--;
                    failed_count++;
                }
                continue;
            }

            switch (clients[i].state) {
            case CLIENT_CONNECTING:
                FD_SET(fd, &writefds);
                FD_SET(fd, &errfds);
                break;
            case CLIENT_CONNECTED: {
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
            }
            case CLIENT_SENDING: {
                FD_SET(fd, &writefds);
                break;
            }
            case CLIENT_SENT: {
            }
            case CLIENT_RECEIVING: {
                FD_SET(fd, &readfds);
                break;
            }
            default: {
                break;
            }
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
        for (int i = 0; i < scan_limit && n > 0; i++) {
            int fd = clients[i].fd;
            if (fd < 0)
                continue;

            // Check for errors
            if (FD_ISSET(fd, &errfds)) {
                close(fd);
                // If we have any response bytes, consider the request completed (partial
                // response). Otherwise count as failed (no response received).
                if (clients[i].response_bytes > 0) {
                    clients[i].response_buffer[clients[i].response_bytes] = '\0';
                    clients[i].http_status = parse_http_status(clients[i].response_buffer, clients[i].response_bytes);
                    clock_gettime(CLOCK_MONOTONIC, &clients[i].recv_end);
                    clients[i].response_time_us = timespec_diff_us(&clients[i].recv_start, &clients[i].recv_end);
                    clients[i].total_time_us = timespec_diff_us(&clients[i].connect_start, &clients[i].recv_end);
                    clients[i].state = CLIENT_DONE;
                    active_count--;
                    completed_count++;
                } else {
                    clients[i].state = CLIENT_FAILED;
                    active_count--;
                    failed_count++;
                }
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
                    if (clients[i].response_bytes > 0) {
                        // Partial response received before connect-check failure: treat as done
                        clients[i].response_buffer[clients[i].response_bytes] = '\0';
                        clients[i].http_status =
                            parse_http_status(clients[i].response_buffer, clients[i].response_bytes);
                        clock_gettime(CLOCK_MONOTONIC, &clients[i].recv_end);
                        clients[i].response_time_us = timespec_diff_us(&clients[i].recv_start, &clients[i].recv_end);
                        clients[i].total_time_us = timespec_diff_us(&clients[i].connect_start, &clients[i].recv_end);
                        clients[i].state = CLIENT_DONE;
                        active_count--;
                        completed_count++;
                    } else {
                        clients[i].state = CLIENT_FAILED;
                        active_count--;
                        failed_count++;
                        if (total < 11) {
                            printf("\n--- Client #%d FAILED (connect check) ---\n", i + 1);
                            if (clients[i].request_data)
                                printf("Request:\n%s\n", clients[i].request_data);
                            if (clients[i].response_buffer && clients[i].response_bytes > 0) {
                                printf("Response:\n");
                                fwrite(clients[i].response_buffer, 1, clients[i].response_bytes, stdout);
                                printf("\n");
                            }
                            fflush(stdout);
                        }
                    }
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
                        // If we already have response bytes, consider it a completed (partial)
                        // response. Otherwise mark as failed.
                        if (clients[i].response_bytes > 0) {
                            clients[i].response_buffer[clients[i].response_bytes] = '\0';
                            clients[i].http_status =
                                parse_http_status(clients[i].response_buffer, clients[i].response_bytes);
                            clock_gettime(CLOCK_MONOTONIC, &clients[i].recv_end);
                            clients[i].response_time_us =
                                timespec_diff_us(&clients[i].recv_start, &clients[i].recv_end);
                            clients[i].total_time_us =
                                timespec_diff_us(&clients[i].connect_start, &clients[i].recv_end);
                            clients[i].state = CLIENT_DONE;
                            active_count--;
                            completed_count++;
                            if (total < 11) {
                                printf("\n--- Client #%d DONE (partial, send error) ---\n", i + 1);
                                if (clients[i].request_data)
                                    printf("Request:\n%s\n", clients[i].request_data);
                                if (clients[i].response_buffer && clients[i].response_bytes > 0) {
                                    printf("Partial Response:\n");
                                    fwrite(clients[i].response_buffer, 1, clients[i].response_bytes, stdout);
                                    printf("\n");
                                }
                                fflush(stdout);
                            }
                        } else {
                            clients[i].state = CLIENT_FAILED;
                            active_count--;
                            failed_count++;
                            if (total < 11) {
                                printf("\n--- Client #%d FAILED (send) ---\n", i + 1);
                                if (clients[i].request_data)
                                    printf("Request:\n%s\n", clients[i].request_data);
                                fflush(stdout);
                            }
                        }
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
                            // If we already received some bytes, treat as completed (partial).
                            if (clients[i].response_bytes > 0) {
                                clients[i].response_buffer[clients[i].response_bytes] = '\0';
                                clients[i].http_status =
                                    parse_http_status(clients[i].response_buffer, clients[i].response_bytes);
                                clock_gettime(CLOCK_MONOTONIC, &clients[i].recv_end);
                                clients[i].response_time_us =
                                    timespec_diff_us(&clients[i].recv_start, &clients[i].recv_end);
                                clients[i].total_time_us =
                                    timespec_diff_us(&clients[i].connect_start, &clients[i].recv_end);
                                close(fd);
                                clients[i].state = CLIENT_DONE;
                                active_count--;
                                completed_count++;
                                if (total < 11) {
                                    printf("\n--- Client #%d DONE (partial, recv error) ---\n", i + 1);
                                    if (clients[i].request_data)
                                        printf("Request:\n%s\n", clients[i].request_data);
                                    if (clients[i].response_buffer && clients[i].response_bytes > 0) {
                                        printf("Partial Response:\n");
                                        fwrite(clients[i].response_buffer, 1, clients[i].response_bytes, stdout);
                                        printf("\n");
                                    }
                                    fflush(stdout);
                                }
                            } else {
                                close(fd);
                                clients[i].state = CLIENT_FAILED;
                                active_count--;
                                failed_count++;
                                if (total < 11) {
                                    printf("\n--- Client #%d FAILED (recv) ---\n", i + 1);
                                    if (clients[i].request_data)
                                        printf("Request:\n%s\n", clients[i].request_data);
                                    fflush(stdout);
                                }
                            }
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
                        if (total < 11) {
                            printf("\n--- Client #%d DONE ---\n", i + 1);
                            if (clients[i].request_data)
                                printf("Request:\n%s\n", clients[i].request_data);
                            if (clients[i].response_buffer && clients[i].response_bytes > 0) {
                                printf("Response (HTTP %d):\n", clients[i].http_status);
                                fwrite(clients[i].response_buffer, 1, clients[i].response_bytes, stdout);
                                printf("\n");
                            } else {
                                printf("(no response body)\n");
                            }
                            fflush(stdout);
                        }
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
        double rate = (completed_count + failed_count) * 1000000.0 / (elapsed ? elapsed : 1);

        if (!eternal) {
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
        } else {
            static const char spinner[] = "|/-\\";
            static int spinner_idx = 0;
            spinner_idx = (spinner_idx + 1) % (int)strlen(spinner);
            printf("\r[%c] sent:%lld active:%d done:%d failed:%d | %.0f req/s", spinner[spinner_idx], requests_sent,
                   active_count, completed_count, failed_count, rate);
            fflush(stdout);
        }
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
    for (int i = 0; i < client_capacity; i++) {
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
    if (eternal) {
        printf("Total requests sent: %lld\n", requests_sent);
        printf("Completed:        %d\n", completed_count);
        printf("Failed:           %d\n", failed_count);
    } else {
        printf("Total requests:   %d\n", total);
        printf("Completed:        %d (%.1f%%)\n", completed_count, 100.0 * completed_count / total);
        printf("Failed:           %d (%.1f%%)\n", failed_count, 100.0 * failed_count / total);
    }
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

    // Write detailed logs to file if requested
    if (log_file) {
        fprintf(log_file, "\n=== Results ===\n");
        fprintf(log_file, "Total requests:   %d\n", total);
        fprintf(log_file, "Completed:        %d (%.1f%%)\n", completed_count, 100.0 * completed_count / total);
        fprintf(log_file, "Failed:           %d (%.1f%%)\n", failed_count, 100.0 * failed_count / total);
        fprintf(log_file, "Time elapsed:     %.3f seconds\n", elapsed_sec);
        fprintf(log_file, "Throughput:       %.0f req/sec\n", completed_count / elapsed_sec);

        fprintf(log_file, "\n=== HTTP Status Codes ===\n");
        if (num_unique_codes > 0) {
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
                fprintf(log_file, "  %6d  %3d  %-25s\n", unique_status_counts[i], code, description);
            }
        } else {
            fprintf(log_file, "  No responses received\n");
        }

        if (completed_count > 0) {
            fprintf(log_file, "\n=== Latency Metrics (milliseconds) ===\n");
            fprintf(log_file, "                   p50      p95      p99      max\n");
            fprintf(log_file, "  Connect:    %7.2f  %7.2f  %7.2f  %7.2f\n",
                    calculate_percentile(connect_times, completed_count, 50) / 1000.0,
                    calculate_percentile(connect_times, completed_count, 95) / 1000.0,
                    calculate_percentile(connect_times, completed_count, 99) / 1000.0,
                    connect_times[completed_count - 1] / 1000.0);
            fprintf(log_file, "  Response:   %7.2f  %7.2f  %7.2f  %7.2f\n",
                    calculate_percentile(response_times, completed_count, 50) / 1000.0,
                    calculate_percentile(response_times, completed_count, 95) / 1000.0,
                    calculate_percentile(response_times, completed_count, 99) / 1000.0,
                    response_times[completed_count - 1] / 1000.0);
            fprintf(log_file, "  Total:      %7.2f  %7.2f  %7.2f  %7.2f\n",
                    calculate_percentile(total_times, completed_count, 50) / 1000.0,
                    calculate_percentile(total_times, completed_count, 95) / 1000.0,
                    calculate_percentile(total_times, completed_count, 99) / 1000.0,
                    total_times[completed_count - 1] / 1000.0);
        }

        fprintf(log_file, "\n=== Individual Request Details ===\n");
        for (int i = 0; i < client_capacity; i++) {
            fprintf(log_file, "\n--- Request #%d ---\n", i + 1);

            // Log request type and city (for weather requests)
            const char* backend_name = "";
            if (clients[i].request_type == 0) {
                backend_name = "Weather";
                fprintf(log_file, "Backend: %s (City: %s)\n", backend_name, CITIES[clients[i].city_index].name);
            } else if (clients[i].request_type == 1) {
                backend_name = "Cities";
                fprintf(log_file, "Backend: %s\n", backend_name);
            } else if (clients[i].request_type == 2) {
                backend_name = "Surprise";
                fprintf(log_file, "Backend: %s\n", backend_name);
            }

            if (clients[i].state == CLIENT_DONE) {
                fprintf(log_file, "Status: SUCCESS (HTTP %d)\n", clients[i].http_status);
                fprintf(log_file, "Response Size: %zu bytes\n", clients[i].response_bytes);
                fprintf(log_file, "Connect Time: %.2f ms\n", clients[i].connect_time_us / 1000.0);
                fprintf(log_file, "Response Time: %.2f ms\n", clients[i].response_time_us / 1000.0);
                fprintf(log_file, "Total Time: %.2f ms\n", clients[i].total_time_us / 1000.0);

                if (clients[i].response_buffer) {
                    fprintf(log_file, "\nRequest:\n%s\n", clients[i].request_data);
                    fprintf(log_file, "\nResponse:\n%s\n", clients[i].response_buffer);
                }
            } else if (clients[i].state == CLIENT_FAILED) {
                fprintf(log_file, "Status: FAILED\n");
                fprintf(log_file, "\nRequest:\n%s\n", clients[i].request_data);
            }
        }

        fclose(log_file);
        printf("\nDetailed logs written to: %s\n", log_filename);
    }

    // Keepalive
    if (keepalive_sec > 0) {
        printf("\nKeeping connections alive for %d seconds...\n", keepalive_sec);
        sleep(keepalive_sec);
    }

    // Cleanup
    for (int i = 0; i < client_capacity; i++) {
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
    int exit_code;
    if (eternal) {
        exit_code = 0; // graceful stop on Ctrl-C
    } else {
        exit_code = (completed_count == total) ? 0 : 1;
    }
    return exit_code;
}

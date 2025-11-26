#ifndef GEOCODE_WEATHER_H
#define GEOCODE_WEATHER_H

#include "majjen.h"
#include <stddef.h>

struct w_client; // Forward declaration
struct geocache; // Forward declaration

/*
    Geocode Weather Task
    --------------------
    Async mj_task state machine that:
    1. Takes a city name from w_client
    2. Fetches coordinates from Open-Meteo Geocoding API (non-blocking)
    3. Fetches weather data for those coordinates (non-blocking)
    4. Fills w_client response_body with JSON result

    Runs as a scheduler task alongside the client task.
*/

#define GEOCODE_API_URL "http://geocoding-api.open-meteo.com/v1/search?name=%s&count=1&language=en&format=json"
#define FORECAST_API_URL                                                                                               \
    "http://api.open-meteo.com/v1/"                                                                                    \
    "forecast?latitude=%.6f&longitude=%.6f&current=temperature_2m,relative_humidity_2m,apparent_temperature,is_day,"   \
    "precipitation,rain,showers,snowfall,weather_"                                                                     \
    "code,cloud_cover,pressure_msl,surface_pressure,wind_speed_10m,wind_direction_10m,wind_gusts_10m"

// State machine states
typedef enum {
    GW_STATE_INIT,
    GW_STATE_GEOCODE_RESOLVE, // DNS resolve for geocoding API
    GW_STATE_GEOCODE_CONNECT, // Connect socket to geocoding API
    GW_STATE_GEOCODE_SEND,    // Send HTTP request
    GW_STATE_GEOCODE_RECV,    // Receive HTTP response
    GW_STATE_GEOCODE_PARSE,   // Parse JSON, extract lat/lon
    GW_STATE_WEATHER_RESOLVE, // DNS resolve for weather API
    GW_STATE_WEATHER_CONNECT, // Connect socket to weather API
    GW_STATE_WEATHER_SEND,    // Send HTTP request
    GW_STATE_WEATHER_RECV,    // Receive HTTP response
    GW_STATE_WEATHER_PARSE,   // Parse and format response
    GW_STATE_DONE,
    GW_STATE_ERROR
} gw_state_t;

// Error codes
typedef enum {
    GW_ERROR_NONE = 0,
    GW_ERROR_INVALID_CITY,
    GW_ERROR_DNS_FAILED,
    GW_ERROR_CONNECT_FAILED,
    GW_ERROR_SEND_FAILED,
    GW_ERROR_RECV_FAILED,
    GW_ERROR_PARSE_FAILED,
    GW_ERROR_FORBIDDEN,
    GW_ERROR_CITY_NOT_FOUND,
    GW_ERROR_TIMEOUT,
    GW_ERROR_MEMORY
} gw_error_t;

// HTTP connection context for async operations
typedef struct {
    int sockfd;
    char host[256];
    char path[512];
    int port;

    char* request;
    size_t request_len;
    size_t request_sent;

    char* response_buffer;
    size_t response_size;
    size_t response_capacity;

    int headers_done;
    int is_chunked;
    size_t content_length;
} gw_http_ctx_t;

// Main task context
typedef struct {
    struct w_client* client;   // The client we're fetching for
    struct geocache* geocache; // Shared geocache (owned by server/main)

    gw_state_t state;
    gw_error_t error;
    int cache_hit; // Whether coordinates came from cache

    // Input (copied from client->requested_city)
    char city_name[128];

    // Geocode result
    double latitude;
    double longitude;
    char resolved_city[128];

    /* Whether we've already retried the geocode lookup with the Å->ä
     * normalization. This prevents infinite retry loops.
     */
    int tried_a_umlaut_normalization;

    // HTTP contexts for async requests
    gw_http_ctx_t geocode_http;
    gw_http_ctx_t weather_http;

} geocode_weather_ctx_t;

// Create a geocode_weather task for the given client
// The task will read city from client->requested_city
// On completion, it fills client->response_body and sets client->state to W_CLIENT_SENDING
// geocache can be NULL (will skip caching)
mj_task* geocode_weather_task_create(struct w_client* client, struct geocache* geocache);

// Create a geocode_weather task with pre-resolved coordinates (cache hit)
// Skips the geocode API call entirely, goes straight to weather fetch
mj_task* geocode_weather_task_create_with_coords(struct w_client* client,
                                                 struct geocache* geocache,
                                                 double latitude,
                                                 double longitude,
                                                 const char* resolved_name);

// Get error description string
const char* geocode_weather_error_str(gw_error_t error);

#endif // GEOCODE_WEATHER_H

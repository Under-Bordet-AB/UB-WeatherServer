#define _GNU_SOURCE

#include "geocode_weather.h"
#include "geocache.h"
#include "global_defines.h"
#include "http_msg_builder.h"
#include "ui.h"
#include "utils.h"
#include "w_client.h"
#include "weathercache.h"
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <jansson.h>
#include <math.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <unistd.h>

#define HTTP_BUFFER_INITIAL_SIZE 4096
#define HTTP_REQUEST_MAX_SIZE 1024

// Forward declarations
static void gw_task_run(mj_scheduler* scheduler, void* ctx);
static void gw_task_cleanup(mj_scheduler* scheduler, void* ctx);

static int gw_http_ctx_init(gw_http_ctx_t* http);
static void gw_http_ctx_cleanup(gw_http_ctx_t* http);
static int gw_http_ctx_reset(gw_http_ctx_t* http);

static int gw_build_geocode_request(geocode_weather_ctx_t* ctx, int apply_umlaut_normalize);
static int gw_build_weather_request(geocode_weather_ctx_t* ctx);
static int gw_connect_nonblocking(gw_http_ctx_t* http);
static int gw_send_request(gw_http_ctx_t* http);
static int gw_recv_response(geocode_weather_ctx_t* gw, gw_http_ctx_t* http);
static int gw_parse_geocode_response(geocode_weather_ctx_t* ctx);
static int gw_parse_weather_response(geocode_weather_ctx_t* ctx);

// ============================================================================
// Error strings
// ============================================================================

const char* geocode_weather_error_str(gw_error_t error) {
    switch (error) {
    case GW_ERROR_NONE:
        return "No error";
    case GW_ERROR_INVALID_CITY:
        return "Invalid city name";
    case GW_ERROR_DNS_FAILED:
        return "DNS resolution failed";
    case GW_ERROR_CONNECT_FAILED:
        return "Connection failed";
    case GW_ERROR_SEND_FAILED:
        return "Send failed";
    case GW_ERROR_RECV_FAILED:
        return "Receive failed";
    case GW_ERROR_PARSE_FAILED:
        return "JSON parse failed";
    case GW_ERROR_CITY_NOT_FOUND:
        return "City not found";
    case GW_ERROR_TIMEOUT:
        return "Request timeout";
    case GW_ERROR_MEMORY:
        return "Memory allocation failed";
    default:
        return "Unknown error";
    }
}

// ============================================================================
// HTTP context helpers
// ============================================================================

static int gw_http_ctx_init(gw_http_ctx_t* http) {
    if (!http)
        return -1;

    memset(http, 0, sizeof(gw_http_ctx_t));
    http->sockfd = -1;
    http->port = 80;

    http->response_buffer = malloc(HTTP_BUFFER_INITIAL_SIZE);
    if (!http->response_buffer)
        return -1;

    http->response_capacity = HTTP_BUFFER_INITIAL_SIZE;
    http->response_size = 0;
    http->response_buffer[0] = '\0';

    return 0;
}

static void gw_http_ctx_cleanup(gw_http_ctx_t* http) {
    if (!http)
        return;

    if (http->sockfd >= 0) {
        close(http->sockfd);
        http->sockfd = -1;
    }

    free(http->request);
    http->request = NULL;

    free(http->response_buffer);
    http->response_buffer = NULL;
}

static int gw_http_ctx_reset(gw_http_ctx_t* http) {
    if (!http)
        return -1;

    if (http->sockfd >= 0) {
        close(http->sockfd);
        http->sockfd = -1;
    }

    free(http->request);
    http->request = NULL;
    http->request_len = 0;
    http->request_sent = 0;

    http->response_size = 0;
    if (http->response_buffer)
        http->response_buffer[0] = '\0';

    http->headers_done = 0;
    http->is_chunked = 0;
    http->content_length = 0;

    memset(http->host, 0, sizeof(http->host));
    memset(http->path, 0, sizeof(http->path));

    return 0;
}

// ============================================================================
// Task creation
// ============================================================================

mj_task* geocode_weather_task_create(struct w_client* client, struct geocache* geocache) {
    if (!client)
        return NULL;

    mj_task* task = calloc(1, sizeof(mj_task));
    if (!task)
        return NULL;

    geocode_weather_ctx_t* ctx = calloc(1, sizeof(geocode_weather_ctx_t));
    if (!ctx) {
        free(task);
        return NULL;
    }

    // Initialize context
    ctx->client = client;
    ctx->geocache = geocache;
    ctx->state = GW_STATE_INIT;
    ctx->error = GW_ERROR_NONE;
    ctx->cache_hit = 0;

    // Copy city name from client
    strncpy(ctx->city_name, client->req_location, sizeof(ctx->city_name) - 1);
    ctx->city_name[sizeof(ctx->city_name) - 1] = '\0';

    // Initialize HTTP contexts
    if (gw_http_ctx_init(&ctx->geocode_http) != 0 || gw_http_ctx_init(&ctx->weather_http) != 0) {
        gw_http_ctx_cleanup(&ctx->geocode_http);
        gw_http_ctx_cleanup(&ctx->weather_http);
        free(ctx);
        free(task);
        return NULL;
    }

    // Set up task
    task->ctx = ctx;
    task->create = NULL;
    task->run = gw_task_run;
    task->destroy = gw_task_cleanup;

    return task;
}

mj_task* geocode_weather_task_create_with_coords(struct w_client* client,
                                                 struct geocache* geocache,
                                                 double latitude,
                                                 double longitude,
                                                 const char* resolved_name) {
    if (!client)
        return NULL;

    mj_task* task = calloc(1, sizeof(mj_task));
    if (!task)
        return NULL;

    geocode_weather_ctx_t* ctx = calloc(1, sizeof(geocode_weather_ctx_t));
    if (!ctx) {
        free(task);
        return NULL;
    }

    // Initialize context with pre-resolved coordinates
    ctx->client = client;
    ctx->geocache = geocache;
    ctx->state = GW_STATE_WEATHER_CONNECT; // Skip geocode, go straight to weather
    ctx->error = GW_ERROR_NONE;
    ctx->cache_hit = 1;

    // Copy city name from client
    strncpy(ctx->city_name, client->req_location, sizeof(ctx->city_name) - 1);
    ctx->city_name[sizeof(ctx->city_name) - 1] = '\0';

    // Set pre-resolved coordinates (normalize to 4 decimals)
    ctx->latitude = round(latitude * 10000.0) / 10000.0;
    ctx->longitude = round(longitude * 10000.0) / 10000.0;
    if (resolved_name) {
        strncpy(ctx->resolved_city, resolved_name, sizeof(ctx->resolved_city) - 1);
        ctx->resolved_city[sizeof(ctx->resolved_city) - 1] = '\0';
    }

    // Initialize HTTP contexts (only weather_http will be used)
    if (gw_http_ctx_init(&ctx->geocode_http) != 0 || gw_http_ctx_init(&ctx->weather_http) != 0) {
        gw_http_ctx_cleanup(&ctx->geocode_http);
        gw_http_ctx_cleanup(&ctx->weather_http);
        free(ctx);
        free(task);
        return NULL;
    }

    // Build the weather request now since we have coordinates
    if (gw_build_weather_request(ctx) != 0) {
        gw_http_ctx_cleanup(&ctx->geocode_http);
        gw_http_ctx_cleanup(&ctx->weather_http);
        free(ctx);
        free(task);
        return NULL;
    }

    // Set up task
    task->ctx = ctx;
    task->create = NULL;
    task->run = gw_task_run;
    task->destroy = gw_task_cleanup;

    return task;
}

// ============================================================================
// Task destroy
// ============================================================================

static void gw_task_cleanup(mj_scheduler* scheduler, void* ctx) {
    (void)scheduler;

    geocode_weather_ctx_t* gw = (geocode_weather_ctx_t*)ctx;
    if (!gw)
        return;

    gw_http_ctx_cleanup(&gw->geocode_http);
    gw_http_ctx_cleanup(&gw->weather_http);

    // Context itself is freed by scheduler
}

// ============================================================================
// Build HTTP requests
// ============================================================================

static int gw_build_geocode_request(geocode_weather_ctx_t* ctx, int apply_umlaut_normalize) {
    if (!ctx || ctx->city_name[0] == '\0')
        return -1;

    // URL encode city name (%-encode any byte that is not an unreserved character)
    // Decode any percent-encoded bytes and lowercase/normalize Swedish
    // characters so upstream receives a consistent representation.
    char city_copy[512];
    strncpy(city_copy, ctx->city_name, sizeof(city_copy) - 1);
    city_copy[sizeof(city_copy) - 1] = '\0';
    // Decode any %-encoding (so sequences like %C3%85 become raw UTF-8)
    utils_convert_utf8_hex_to_utf8_bytes(city_copy);
    // Lowercase only Swedish multibyte letters (Å/Ä/Ö -> å/ä/ö)
    utils_lowercase_swedish_letters(city_copy);
    // Optionally apply Å -> ä normalization if requested (legacy fallback)
    if (apply_umlaut_normalize) {
        utils_normalize_swedish_a_umlaut(city_copy);
    }

    // This correctly handles UTF-8 multi-byte sequences by percent-encoding each byte.
    char encoded_city[512];
    size_t j = 0;
    for (size_t i = 0; city_copy[i] != '\0' && j + 4 < sizeof(encoded_city); i++) {
        unsigned char c = (unsigned char)city_copy[i];
        // Unreserved characters per RFC 3986: ALPHA / DIGIT / '-' / '.' / '_' / '~'
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '.' ||
            c == '_' || c == '~') {
            encoded_city[j++] = c;
        } else if (c == ' ') {
            // encode space as %20
            encoded_city[j++] = '%';
            encoded_city[j++] = '2';
            encoded_city[j++] = '0';
        } else {
            // Percent-encode this byte
            int n = snprintf(&encoded_city[j], 4, "%%%02X", c);
            if (n != 3) {
                // snprintf failure or not enough space
                break;
            }
            j += 3;
        }
    }
    encoded_city[j] = '\0';

    // Set host and path
    strncpy(ctx->geocode_http.host, "geocoding-api.open-meteo.com", sizeof(ctx->geocode_http.host) - 1);
    snprintf(ctx->geocode_http.path, sizeof(ctx->geocode_http.path),
             "/v1/search?name=%s&count=1&language=en&format=json", encoded_city);
    ctx->geocode_http.port = 80;

    // Build HTTP request
    ctx->geocode_http.request = malloc(HTTP_REQUEST_MAX_SIZE);
    if (!ctx->geocode_http.request)
        return -1;

    ctx->geocode_http.request_len = snprintf(ctx->geocode_http.request, HTTP_REQUEST_MAX_SIZE,
                                             "GET %s HTTP/1.1\r\n"
                                             "Host: %s\r\n"
                                             "Connection: close\r\n"
                                             "User-Agent: WeatherServer/1.0\r\n"
                                             "\r\n",
                                             ctx->geocode_http.path, ctx->geocode_http.host);

    ctx->geocode_http.request_sent = 0;

    return 0;
}

// Return non-zero if the given string contains UTF-8 sequences for Å or å
// (0xC3 0x85, 0xC3 0xA5). We use this to decide whether to attempt the
// Å->ä normalization retry; avoid retrying for names that don't contain
// these characters (e.g., "Engelholm").
static int gw_city_contains_a_umlaut(const char* s) {
    if (!s)
        return 0;
    for (size_t i = 0; s[i]; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c == 0xC3 && s[i + 1]) {
            unsigned char n = (unsigned char)s[i + 1];
            if (n == 0x85 || n == 0xA5)
                return 1;
        }
    }
    return 0;
}

static int gw_build_weather_request(geocode_weather_ctx_t* ctx) {
    if (!ctx)
        return -1;

    // Never build weather request for invalid coordinates
    if (ctx->latitude == 0.0 && ctx->longitude == 0.0) {
        return -1;
    }

    // Set host and path
    strncpy(ctx->weather_http.host, "api.open-meteo.com", sizeof(ctx->weather_http.host) - 1);
    ctx->weather_http.host[sizeof(ctx->weather_http.host) - 1] = '\0';

    snprintf(ctx->weather_http.path, sizeof(ctx->weather_http.path),
             "/v1/forecast?latitude=%.6f&longitude=%.6f"
             "&current_weather=true"
             "&hourly=temperature_2m,relativehumidity_2m,apparent_temperature,"
             "precipitation,rain,showers,snowfall,weathercode,cloudcover,"
             "pressure_msl,surface_pressure,windspeed_10m,winddirection_10m,windgusts_10m"
             "&timezone=auto",
             ctx->latitude, ctx->longitude);
    ctx->weather_http.port = 80;

    ctx->weather_http.request = malloc(HTTP_REQUEST_MAX_SIZE);
    if (!ctx->weather_http.request)
        return -1;

    ctx->weather_http.request_len = snprintf(ctx->weather_http.request, HTTP_REQUEST_MAX_SIZE,
                                             "GET %s HTTP/1.1\r\n"
                                             "Host: %s\r\n"
                                             "Connection: close\r\n"
                                             "User-Agent: WeatherServer/1.0\r\n"
                                             "Accept: application/json\r\n"
                                             "\r\n",
                                             ctx->weather_http.path, ctx->weather_http.host);

    /* handle allocation/snprintf errors/truncation as needed in your surrounding code */
    ctx->weather_http.request_sent = 0;

    return 0;
}

// ============================================================================
// Non-blocking connect
// ============================================================================

static int gw_connect_nonblocking(gw_http_ctx_t* http) {
    if (!http || http->host[0] == '\0')
        return -1;

    // Already connected?
    if (http->sockfd >= 0) {
        // Check if connection completed
        int error = 0;
        socklen_t len = sizeof(error);
        if (getsockopt(http->sockfd, SOL_SOCKET, SO_ERROR, &error, &len) < 0 || error != 0) {
            close(http->sockfd);
            http->sockfd = -1;
            return -1; // Connection failed
        }
        return 1; // Connected
    }

    // DNS lookup (blocking for now - TODO: use getaddrinfo_a for async)
    struct hostent* server = gethostbyname(http->host);
    if (!server)
        return -1;

    // Create socket
    http->sockfd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (http->sockfd < 0)
        return -1;

    // Set up address
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    memcpy(&server_addr.sin_addr.s_addr, server->h_addr_list[0], server->h_length);
    server_addr.sin_port = htons(http->port);

    // Attempt non-blocking connect
    int ret = connect(http->sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr));
    if (ret < 0) {
        if (errno == EINPROGRESS) {
            return 0; // Connection in progress
        }
        close(http->sockfd);
        http->sockfd = -1;
        return -1; // Connection failed
    }

    return 1; // Connected immediately
}

// ============================================================================
// Non-blocking send
// ============================================================================

static int gw_send_request(gw_http_ctx_t* http) {
    if (!http || http->sockfd < 0 || !http->request)
        return -1;

    size_t remaining = http->request_len - http->request_sent;
    if (remaining == 0)
        return 1; // All sent

    ssize_t sent = send(http->sockfd, http->request + http->request_sent, remaining, MSG_NOSIGNAL);

    if (sent < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return 0; // Would block, try again later
        return -1;    // Error
    }

    http->request_sent += sent;

    return (http->request_sent >= http->request_len) ? 1 : 0;
}

// ============================================================================
// Non-blocking receive
// ============================================================================

static int gw_recv_response(geocode_weather_ctx_t* gw, gw_http_ctx_t* http) {
    if (!http || http->sockfd < 0)
        return -1;

    char buffer[4096];
    ssize_t bytes = recv(http->sockfd, buffer, sizeof(buffer) - 1, 0);

    if (bytes < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return 0; // No data yet

        // Log detailed recv error for debugging and mark gw error if available
        if (gw && gw->client) {
            ui_print_backend_error(gw->client, "GeocodeWeather", strerror(errno));
        }
        return -1; // Error
    }

    if (bytes == 0) { // connection closed by peer
        // If upstream returned HTTP 403 body, mark as forbidden so caller can build correct response
        if (http->response_buffer && (strstr(http->response_buffer, "HTTP/1.1 403") != NULL ||
                                      strstr(http->response_buffer, "HTTP/1.0 403") != NULL ||
                                      strstr(http->response_buffer, "403 Forbidden") != NULL)) {
            if (gw)
                gw->error = GW_ERROR_FORBIDDEN;
            return -1; // Signal error to caller so state machine moves to GW_STATE_ERROR
        }

        return 1; // Done receiving normally
    }

    buffer[bytes] = '\0';

    // Grow buffer if needed
    if (http->response_size + bytes >= http->response_capacity) {
        size_t new_capacity = http->response_capacity * 2;
        while (new_capacity < http->response_size + bytes + 1)
            new_capacity *= 2;

        char* new_buffer = realloc(http->response_buffer, new_capacity);
        if (!new_buffer)
            return -1;

        http->response_buffer = new_buffer;
        http->response_capacity = new_capacity;
    }

    memcpy(http->response_buffer + http->response_size, buffer, bytes);
    http->response_size += bytes;
    http->response_buffer[http->response_size] = '\0';

    return 0; // More data may come
}

// ============================================================================
// Extract HTTP body from response
// ============================================================================

static char* gw_extract_body(const char* response) {
    if (!response)
        return NULL;

    const char* body_start = strstr(response, "\r\n\r\n");
    if (!body_start)
        return NULL;

    body_start += 4;

    // Check for chunked encoding
    const char* te = strcasestr(response, "Transfer-Encoding:");
    if (te && te < body_start) {
        const char* chunked = strcasestr(te, "chunked");
        if (chunked && chunked < body_start) {
            // Decode chunked response
            size_t decoded_capacity = strlen(body_start) + 1;
            char* decoded = malloc(decoded_capacity);
            if (!decoded)
                return NULL;

            size_t decoded_size = 0;
            const char* ptr = body_start;

            while (*ptr) {
                // Skip whitespace
                while (*ptr == ' ' || *ptr == '\t' || *ptr == '\r' || *ptr == '\n')
                    ptr++;
                if (!*ptr)
                    break;

                // Read chunk size
                char* endptr;
                long chunk_size = strtol(ptr, &endptr, 16);
                if (endptr == ptr || chunk_size < 0) {
                    free(decoded);
                    return NULL;
                }

                // Skip to chunk data
                ptr = strstr(endptr, "\r\n");
                if (!ptr) {
                    free(decoded);
                    return NULL;
                }
                ptr += 2;

                if (chunk_size == 0)
                    break; // Last chunk

                // Copy chunk data
                memcpy(decoded + decoded_size, ptr, chunk_size);
                decoded_size += chunk_size;
                ptr += chunk_size;

                // Skip trailing CRLF
                if (ptr[0] == '\r' && ptr[1] == '\n')
                    ptr += 2;
            }

            decoded[decoded_size] = '\0';
            return decoded;
        }
    }

    // Not chunked, just return body
    return strdup(body_start);
}

// ============================================================================
// Parse geocode response
// ============================================================================

static int gw_parse_geocode_response(geocode_weather_ctx_t* ctx) {
    if (!ctx)
        return -1;

    // Check HTTP status
    const char* status_line = ctx->geocode_http.response_buffer;
    if (!status_line || strstr(status_line, "200") == NULL) {
        if (strstr(status_line, "404") != NULL || strstr(status_line, "400") != NULL) {
            ctx->error = GW_ERROR_CITY_NOT_FOUND;
        } else {
            ctx->error = GW_ERROR_RECV_FAILED;
        }
        return -1;
    }

    char* body = gw_extract_body(ctx->geocode_http.response_buffer);
    if (!body)
        return -1;

    json_error_t error;
    json_t* root = json_loads(body, 0, &error);
    free(body);

    if (!root)
        return -1;

    // Get results array
    json_t* results = json_object_get(root, "results");
    if (!json_is_array(results) || json_array_size(results) == 0) {
        json_decref(root);
        ctx->error = GW_ERROR_CITY_NOT_FOUND;
        return -1;
    }

    // Get first result
    json_t* first = json_array_get(results, 0);
    if (!json_is_object(first)) {
        json_decref(root);
        return -1;
    }

    // Extract latitude, longitude, name
    json_t* lat = json_object_get(first, "latitude");
    json_t* lon = json_object_get(first, "longitude");
    json_t* name = json_object_get(first, "name");

    if (!json_is_number(lat) || !json_is_number(lon)) {
        json_decref(root);
        return -1;
    }

    // Normalize coordinates to 4 decimal places
    ctx->latitude = round(json_number_value(lat) * 10000.0) / 10000.0;
    ctx->longitude = round(json_number_value(lon) * 10000.0) / 10000.0;

    // Reject invalid coordinates (e.g., 0.0,0.0 indicates geocoding failure)
    if (ctx->latitude == 0.0 && ctx->longitude == 0.0) {
        json_decref(root);
        ctx->error = GW_ERROR_CITY_NOT_FOUND;
        return -1;
    }

    if (json_is_string(name)) {
        strncpy(ctx->resolved_city, json_string_value(name), sizeof(ctx->resolved_city) - 1);
        ctx->resolved_city[sizeof(ctx->resolved_city) - 1] = '\0';
        /* Normalize resolved city to lowercase for consistent internal keys */
        utils_to_lowercase(ctx->resolved_city);
    }

    json_decref(root);
    return 0;
}

// ============================================================================
// Parse weather response and build client response
// ============================================================================

static int gw_parse_weather_response(geocode_weather_ctx_t* ctx) {
    if (!ctx || !ctx->client)
        return -1;

    // Check HTTP status
    const char* status_line = ctx->weather_http.response_buffer;
    if (!status_line || strstr(status_line, "200") == NULL) {
        ctx->error = GW_ERROR_RECV_FAILED;
        return -1;
    }

    char* body = gw_extract_body(ctx->weather_http.response_buffer);
    if (!body)
        return -1;

    // Validate it's valid JSON
    json_error_t error;
    json_t* root = json_loads(body, 0, &error);
    if (!root) {
        free(body);
        return -1;
    }

    // Add city name to the response (use lowercase versions)
    /* Ensure we send lowercase city/req_location so downstream consumers
     * and cache keys remain consistent. */
    /* Ensure city/req_location fields contain normalized Swedish letters
     * but preserve ASCII case (user's original ASCII casing). */
    utils_lowercase_swedish_letters(ctx->city_name);
    utils_lowercase_swedish_letters(ctx->resolved_city);
    json_object_set_new(root, "city", json_string(ctx->resolved_city));
    json_object_set_new(root, "req_location", json_string(ctx->city_name));

    // Convert back to string
    char* final_json = json_dumps(root, JSON_COMPACT);
    json_decref(root);
    free(body);

    if (!final_json)
        return -1;

    // Lowercase the final JSON payload to be defensive about upstream casing
    if (final_json) {
        utils_to_lowercase(final_json);
    }

    // Build HTTP response for client
    ctx->client->response_body = http_msg_200_ok_json(final_json);
    free(final_json);

    if (!ctx->client->response_body)
        return -1;

    ctx->client->response_len = strlen(ctx->client->response_body);
    ctx->client->response_sent = 0;

    return 0;
}

// ============================================================================
// Build error response for client
// ============================================================================

static void gw_build_error_response(geocode_weather_ctx_t* ctx) {
    if (!ctx || !ctx->client)
        return;

    // Free any existing response body
    if (ctx->client->response_body) {
        free(ctx->client->response_body);
        ctx->client->response_body = NULL;
    }

    const char* error_msg = geocode_weather_error_str(ctx->error);
    /* For misspellings / not-found results return a clearer, user-facing
     * message rather than the internal error string. This makes it obvious
     * to API consumers that the requested location could not be resolved.
     */
    if (ctx->error == GW_ERROR_CITY_NOT_FOUND) {
        error_msg = "Location not found";
    }

    // Build JSON error
    json_t* error_obj = json_object();
    json_object_set_new(error_obj, "error", json_string(error_msg));
    json_object_set_new(error_obj, "city", json_string(ctx->city_name));

    char* error_json = json_dumps(error_obj, JSON_COMPACT);
    json_decref(error_obj);

    if (ctx->error == GW_ERROR_CITY_NOT_FOUND) {
        ctx->client->response_body = http_msg_404_not_found(error_json ? error_json : error_msg);
    } else {
        ctx->client->response_body = http_msg_500_internal_error(error_json ? error_json : error_msg);
    }

    free(error_json);

    if (ctx->client->response_body) {
        ctx->client->response_len = strlen(ctx->client->response_body);
        ctx->client->response_sent = 0;
    }
}

// ============================================================================
// Main state machine run function
// ============================================================================

static void gw_task_run(mj_scheduler* scheduler, void* ctx) {
    if (!scheduler || !ctx)
        return;

    geocode_weather_ctx_t* gw = (geocode_weather_ctx_t*)ctx;
    w_client* client = gw->client;

    switch (gw->state) {

    case GW_STATE_INIT: {
        ui_print_backend_init(client, "GeocodeWeather");

        if (gw->city_name[0] == '\0') {
            gw->error = GW_ERROR_INVALID_CITY;
            gw->state = GW_STATE_ERROR;
            break;
        }

        // build geocode request
        if (gw_build_geocode_request(gw, 0) != 0) {
            gw->error = GW_ERROR_MEMORY;
            gw->state = GW_STATE_ERROR;
            break;
        }

        gw->state = GW_STATE_GEOCODE_CONNECT;
        ui_print_backend_state(client, "GeocodeWeather", "geocache miss, starting geocode lookup");
        break;
    }

    case GW_STATE_GEOCODE_CONNECT: {
        int ret = gw_connect_nonblocking(&gw->geocode_http);
        if (ret < 0) {
            gw->error = GW_ERROR_CONNECT_FAILED;
            gw->state = GW_STATE_ERROR;
            ui_print_backend_error(client, "GeocodeWeather", "geocode connect failed");
        } else if (ret > 0) {
            gw->state = GW_STATE_GEOCODE_SEND;
            ui_print_backend_state(client, "GeocodeWeather", "connected to geocode API");
        }
        // ret == 0: still connecting, stay in this state
        break;
    }

    case GW_STATE_GEOCODE_SEND: {
        int ret = gw_send_request(&gw->geocode_http);
        if (ret < 0) {
            gw->error = GW_ERROR_SEND_FAILED;
            gw->state = GW_STATE_ERROR;
            ui_print_backend_error(client, "GeocodeWeather", "geocode send failed");
        } else if (ret > 0) {
            gw->state = GW_STATE_GEOCODE_RECV;
            ui_print_backend_state(client, "GeocodeWeather", "sent geocode request");
        }
        break;
    }

    case GW_STATE_GEOCODE_RECV: {
        int ret = gw_recv_response(gw, &gw->geocode_http);
        if (ret < 0) {
            gw->error = GW_ERROR_RECV_FAILED;
            gw->state = GW_STATE_ERROR;
            ui_print_backend_error(client, "GeocodeWeather", "geocode recv failed");
        } else if (ret > 0) {
            gw->state = GW_STATE_GEOCODE_PARSE;
            ui_print_backend_state(client, "GeocodeWeather", "received geocode response");
        }
        break;
    }

    case GW_STATE_GEOCODE_PARSE: {
        if (gw_parse_geocode_response(gw) != 0) {
            /* Log detailed failure info: outgoing request path and a snippet
             * of the API response body so it's clear whether the location
             * simply doesn't exist in the geocoding API or if we got a
             * non-JSON/forbidden response.
             */

            // set lat & long to insane values so we can check later

            const char* path = gw->geocode_http.path[0] ? gw->geocode_http.path : "(none)";
            char snippet[512] = {0};
            char* body = gw_extract_body(gw->geocode_http.response_buffer);
            if (body) {
                strncpy(snippet, body, sizeof(snippet) - 1);
                size_t blen = strlen(body);
                if (blen > sizeof(snippet) - 4) {
                    snippet[sizeof(snippet) - 4] = '\0';
                    strcat(snippet, "...");
                }
                free(body);
            } else if (gw->geocode_http.response_buffer && gw->geocode_http.response_size > 0) {
                strncpy(snippet, gw->geocode_http.response_buffer, sizeof(snippet) - 1);
                size_t blen = strlen(snippet);
                if (blen > sizeof(snippet) - 4) {
                    snippet[sizeof(snippet) - 4] = '\0';
                    strcat(snippet, "...");
                }
            } else {
                strncpy(snippet, "(no body)", sizeof(snippet) - 1);
            }

            char msg[768];
            snprintf(msg, sizeof(msg), "geocode parse failed - request path: %s", path);
            ui_print_backend_error(client, "GeocodeWeather", msg);
            ui_print_backend_state(client, "GeocodeWeather", snippet);
            /* If city was not found, retry once with Å->ä normalization. This is
             * a pragmatic fallback for some legacy names (e.g. Torshälla) where
             * different sources use different characters. Don't retry more than
             * once to avoid loops.
             */
            if (gw->error == GW_ERROR_CITY_NOT_FOUND && !gw->tried_a_umlaut_normalization &&
                gw_city_contains_a_umlaut(gw->city_name)) {
                gw->tried_a_umlaut_normalization = 1;
                ui_print_backend_state(client, "GeocodeWeather", "retrying geocode with alternate normalization");
                gw_http_ctx_reset(&gw->geocode_http);
                if (gw_build_geocode_request(gw, 1) != 0) {
                    gw->error = GW_ERROR_MEMORY;
                    gw->state = GW_STATE_ERROR;
                    break;
                }
                gw->state = GW_STATE_GEOCODE_CONNECT;
                break;
            }

            if (gw->error == GW_ERROR_NONE)
                gw->error = GW_ERROR_PARSE_FAILED;
            gw->state = GW_STATE_ERROR;
            ui_print_backend_error(client, "GeocodeWeather", "geocode parse failed");
            break;
        }

        ui_print_backend_state(client, "GeocodeWeather", "parsed coordinates");

        // Save to geocache for future lookups. Use the exact user-provided
        // city string as the cache key/name so lookups remain byte-for-byte
        // exact (the caller is responsible for spelling).
        if (gw->geocache) {
            geocache_insert(gw->geocache, gw->city_name, gw->latitude, gw->longitude, gw->city_name);
            // Optionally save to disk immediately, or batch saves
            geocache_save(gw->geocache);
        }

        // Clean up geocode HTTP context and prepare for weather request
        gw_http_ctx_reset(&gw->geocode_http);

        if (gw_build_weather_request(gw) != 0) {
            gw->error = GW_ERROR_MEMORY;
            gw->state = GW_STATE_ERROR;
            break;
        }

        gw->state = GW_STATE_WEATHER_CONNECT;
        break;
    }

    case GW_STATE_WEATHER_CONNECT: {
        // Before attempting network, check weather cache for a current entry
        char norm[128];
        /* Use the exact user-supplied city name for weather cache lookup so
         * cache keys correspond to what the API client requested. */
        const char* name_for_cache = gw->city_name;
        /* Use exact city name as provided/resolved for weather cache lookup.
         * Do not apply additional normalization; this preserves exact matching
         * between requests and cache files (per user request).
         */
        strncpy(norm, name_for_cache, sizeof(norm) - 1);
        norm[sizeof(norm) - 1] = '\0';
        char* cached_body = NULL;
        if (weathercache_get_by_coords(norm, gw->latitude, gw->longitude, &cached_body) == 0) {
            /*
             * Detect cached upstream error payloads (e.g. rate-limit responses
             * like: {"reason":"Too many concurrent requests","error":true})
             * Some cached files may contain extra debugging/prefix data; use
             * substring search to detect the problematic response and remove
             * the cache so we will attempt a live fetch instead.
             */
            if (strstr(cached_body, "\"Too many concurrent requests\"") != NULL ||
                strstr(cached_body, "\"reason\":\"Too many concurrent requests\"") != NULL) {
                ui_print_backend_error(
                    client, "GeocodeWeather",
                    "cached upstream rate-limit response detected; removing cache and fetching live data");
                /* remove the offending cache file and free the loaded buffer */
                weathercache_remove_by_coords(norm, gw->latitude, gw->longitude);
                free(cached_body);
                /* fall through to perform a live fetch */
            } else {
                /* Parse cached API JSON, add city/req_location fields like live path */
                json_error_t jerr;
                json_t* root = json_loads(cached_body, 0, &jerr);
                char* final_json = NULL;
                if (root) {
                    json_object_set_new(
                        root, "city", json_string((gw->resolved_city[0] != '\0') ? gw->resolved_city : gw->city_name));
                    json_object_set_new(root, "req_location", json_string(gw->city_name));
                    final_json = json_dumps(root, JSON_COMPACT);
                    json_decref(root);
                }

                if (final_json) {
                    gw->client->response_body = http_msg_200_ok_json(final_json);
                    free(final_json);
                } else {
                    /* Fallback: serve raw cached body */
                    gw->client->response_body = http_msg_200_ok_json(cached_body);
                }

                free(cached_body);

                if (gw->client->response_body) {
                    gw->client->response_len = strlen(gw->client->response_body);
                    gw->client->response_sent = 0;
                    /* Log that we served a cached response so output matches live path */
                    ui_print_backend_state(client, "GeocodeWeather", "served cached weather response");
                    ui_print_backend_done(client, "GeocodeWeather");
                }

                gw->state = GW_STATE_DONE;
                break;
            }
        }

        int ret = gw_connect_nonblocking(&gw->weather_http);
        if (ret < 0) {
            gw->error = GW_ERROR_CONNECT_FAILED;
            gw->state = GW_STATE_ERROR;
            ui_print_backend_error(client, "GeocodeWeather", "weather connect failed");
        } else if (ret > 0) {
            gw->state = GW_STATE_WEATHER_SEND;
            ui_print_backend_state(client, "GeocodeWeather", "connected to weather API");
        }
        break;
    }

    case GW_STATE_WEATHER_SEND: {
        int ret = gw_send_request(&gw->weather_http);
        if (ret < 0) {
            gw->error = GW_ERROR_SEND_FAILED;
            gw->state = GW_STATE_ERROR;
            ui_print_backend_error(client, "GeocodeWeather", "weather send failed");
        } else if (ret > 0) {
            gw->state = GW_STATE_WEATHER_RECV;
            ui_print_backend_state(client, "GeocodeWeather", "sent weather request");
        }
        break;
    }

    case GW_STATE_WEATHER_RECV: {
        int ret = gw_recv_response(gw, &gw->weather_http);
        if (ret < 0) {
            gw->error = GW_ERROR_RECV_FAILED;
            gw->state = GW_STATE_ERROR;
            ui_print_backend_error(client, "GeocodeWeather", "weather recv failed");
        } else if (ret > 0) {
            gw->state = GW_STATE_WEATHER_PARSE;
            ui_print_backend_state(client, "GeocodeWeather", "received weather response");
        }
        break;
    }

    case GW_STATE_WEATHER_PARSE: {
        if (gw_parse_weather_response(gw) != 0) {
            gw->error = GW_ERROR_PARSE_FAILED;
            gw->state = GW_STATE_ERROR;
            ui_print_backend_error(client, "GeocodeWeather", "weather parse failed");
            break;
        }
        // Attempt to save the parsed JSON to weather cache.
        // The parsed JSON was added to the client's response in gw_parse_weather_response,
        // but we also created the final JSON string there before wrapping. To persist, rebuild
        // a JSON body by extracting the body from the client's response (strip HTTP headers).
        char* body = gw_extract_body(gw->weather_http.response_buffer);
        if (body) {
            char norm[128];
            /* Save using the exact user-supplied city string so subsequent
             * requests must use the same spelling to hit the cache. */
            const char* name_for_cache = gw->city_name;
            geocache_normalize_name(name_for_cache, norm, sizeof(norm));
            /* Cache the upstream body as-received. We already normalized the
             * `city` and `req_location` fields injected into the live
             * response above; storing the raw upstream body avoids corrupting
             * the JSON and preserves numeric formatting. */
            weathercache_set_by_coords(norm, gw->latitude, gw->longitude, body);
            free(body);
        }

        ui_print_backend_done(client, "GeocodeWeather");
        gw->state = GW_STATE_DONE;
        break;
    }

    case GW_STATE_DONE: {
        // Set client state to sending and remove this task
        client->state = W_CLIENT_SENDING;
        mj_scheduler_task_remove_current(scheduler);
        break;
    }

    case GW_STATE_ERROR: {
        ui_print_backend_error(client, "GeocodeWeather", geocode_weather_error_str(gw->error));
        gw_build_error_response(gw);
        client->state = W_CLIENT_SENDING;
        mj_scheduler_task_remove_current(scheduler);
        break;
    }

    default:
        gw->error = GW_ERROR_PARSE_FAILED;
        gw->state = GW_STATE_ERROR;
        break;
    }
}

#include "ui.h"
#include "http_parser.h"
#include <stdio.h>

// UI printing control flag - can be changed at runtime
int UI_PRINT_ENABLED = 0;

// ANSI color codes (48 colors ordered for maximum contrast and readability)
const char* client_colors[] = {
    "\033[38;5;196m", // Red
    "\033[38;5;51m",  // Cyan
    "\033[38;5;226m", // Yellow
    "\033[38;5;21m",  // Blue
    "\033[38;5;46m",  // Green
    "\033[38;5;201m", // Magenta
    "\033[38;5;214m", // Orange
    "\033[38;5;87m",  // Sky Blue
    "\033[38;5;154m", // Light Green
    "\033[38;5;129m", // Purple
    "\033[38;5;220m", // Gold
    "\033[38;5;39m",  // Deep Blue
    "\033[38;5;160m", // Dark Red
    "\033[38;5;50m",  // Turquoise
    "\033[38;5;190m", // Yellow-Green
    "\033[38;5;93m",  // Purple-Blue
    "\033[38;5;202m", // Orange-Red
    "\033[38;5;45m",  // Bright Cyan
    "\033[38;5;118m", // Lime
    "\033[38;5;165m", // Magenta-Purple
    "\033[38;5;208m", // Dark Orange
    "\033[38;5;33m",  // Dodger Blue
    "\033[38;5;40m",  // Bright Green
    "\033[38;5;199m", // Hot Pink
    "\033[38;5;184m", // Yellow-Orange
    "\033[38;5;27m",  // Ocean Blue
    "\033[38;5;82m",  // Spring Green
    "\033[38;5;135m", // Violet
    "\033[38;5;166m", // Orange Brown
    "\033[38;5;75m",  // Steel Blue
    "\033[38;5;34m",  // Forest Green
    "\033[38;5;205m", // Pink
    "\033[38;5;178m", // Gold Orange
    "\033[38;5;63m",  // Medium Blue
    "\033[38;5;148m", // Olive Green
    "\033[38;5;170m", // Orchid
    "\033[38;5;172m", // Burnt Orange
    "\033[38;5;117m", // Light Blue
    "\033[38;5;76m",  // Chartreuse
    "\033[38;5;141m", // Lavender
    "\033[38;5;209m", // Peach
    "\033[38;5;69m",  // Cornflower Blue
    "\033[38;5;113m", // Yellow Green
    "\033[38;5;177m", // Plum
    "\033[38;5;215m", // Light Orange
    "\033[38;5;81m",  // Aqua
    "\033[38;5;156m", // Pale Green
    "\033[38;5;207m", // Light Magenta
};

void ui_print_timeout(w_client* client, int timeout_sec) {
    if (!UI_PRINT_ENABLED)
        return;
    const char* color = client_colors[client->client_number % NUM_COLORS];
    fprintf(stderr, "%sClient %4zu (active: %4zu, total: %4zu) Connection timeout (%ds)%s\n", color,
            client->client_number, client->server->active_count, client->server->total_clients, timeout_sec,
            COLOR_RESET);
}

void ui_print_read_error(w_client* client, const char* error) {
    if (!UI_PRINT_ENABLED)
        return;
    const char* color = client_colors[client->client_number % NUM_COLORS];
    fprintf(stderr, "%sClient %4zu (active: %4zu, total: %4zu) Read error: %s%s\n", color, client->client_number,
            client->server->active_count, client->server->total_clients, error, COLOR_RESET);
}

void ui_print_connection_closed_by_client(w_client* client) {
    if (!UI_PRINT_ENABLED)
        return;
    const char* color = client_colors[client->client_number % NUM_COLORS];
    fprintf(stderr, "%sClient %4zu (active: %4zu, total: %4zu) Connection closed by client%s\n", color,
            client->client_number, client->server->active_count, client->server->total_clients, COLOR_RESET);
}

void ui_print_received_bytes(w_client* client, ssize_t bytes) {
    if (!UI_PRINT_ENABLED)
        return;
    const char* color = client_colors[client->client_number % NUM_COLORS];
    fprintf(stderr, "%sClient %4zu (active: %4zu, total: %4zu) Received %zd bytes (total: %zu)%s\n", color,
            client->client_number, client->server->active_count, client->server->total_clients, bytes,
            client->bytes_read + bytes, COLOR_RESET);
}

void ui_print_request_too_large(w_client* client) {
    if (!UI_PRINT_ENABLED)
        return;
    const char* color = client_colors[client->client_number % NUM_COLORS];
    fprintf(stderr, "%sClient %4zu (active: %4zu, total: %4zu) ❌ REQUEST TOO LARGE - Buffer full (%zu bytes)%s\n",
            color, client->client_number, client->server->active_count, client->server->total_clients,
            client->bytes_read, COLOR_RESET);
}

void ui_print_bad_request(w_client* client) {
    if (!UI_PRINT_ENABLED)
        return;
    const char* color = client_colors[client->client_number % NUM_COLORS];
    fprintf(stderr, "%sClient %4zu (active: %4zu, total: %4zu) ❌ BAD REQUEST - Failed to parse HTTP request%s\n",
            color, client->client_number, client->server->active_count, client->server->total_clients, COLOR_RESET);
}

void ui_print_request_details(w_client* client) {
    if (!UI_PRINT_ENABLED)
        return;
    const char* color = client_colors[client->client_number % NUM_COLORS];
    http_request* parsed = (http_request*)client->parsed_request;
    fprintf(stderr, "%sClient %4zu (active: %4zu, total: %4zu) Request: %s %s HTTP/%d.%d%s\n", color,
            client->client_number, client->server->active_count, client->server->total_clients,
            request_method_tostring(parsed->method), parsed->url, parsed->protocol / 10, parsed->protocol % 10,
            COLOR_RESET);
}

void ui_print_processing_request(w_client* client) {
    if (!UI_PRINT_ENABLED)
        return;
    const char* color = client_colors[client->client_number % NUM_COLORS];
    fprintf(stderr, "%sClient %4zu (active: %4zu, total: %4zu) Processing request...%s\n", color, client->client_number,
            client->server->active_count, client->server->total_clients, COLOR_RESET);
}

void ui_print_response_details(w_client* client, int code, const char* code_str, size_t response_len) {
    if (!UI_PRINT_ENABLED)
        return;
    const char* color = client_colors[client->client_number % NUM_COLORS];
    fprintf(stderr, "%sClient %4zu (active: %4zu, total: %4zu) Response: %d %s (%zu bytes)%s\n", color,
            client->client_number, client->server->active_count, client->server->total_clients, code, code_str,
            response_len, COLOR_RESET);
}

void ui_print_send_error(w_client* client, const char* error) {
    if (!UI_PRINT_ENABLED)
        return;
    const char* color = client_colors[client->client_number % NUM_COLORS];
    fprintf(stderr, "%sClient %4zu (active: %4zu, total: %4zu) Send error: %s%s\n", color, client->client_number,
            client->server->active_count, client->server->total_clients, error, COLOR_RESET);
}

void ui_print_connection_closed_during_send(w_client* client) {
    if (!UI_PRINT_ENABLED)
        return;
    const char* color = client_colors[client->client_number % NUM_COLORS];
    fprintf(stderr, "%sClient %4zu (active: %4zu, total: %4zu) Connection closed during send%s\n", color,
            client->client_number, client->server->active_count, client->server->total_clients, COLOR_RESET);
}

void ui_print_unknown_state_error(w_client* client, int state) {
    if (!UI_PRINT_ENABLED)
        return;
    const char* color = client_colors[client->client_number % NUM_COLORS];
    fprintf(stderr, "%sClient %4zu (active: %4zu, total: %4zu) ERROR: Unknown state %d, forcing cleanup%s\n", color,
            client->client_number, client->server->active_count, client->server->total_clients, state, COLOR_RESET);
}

void ui_print_creation_error(const char* file, int line, const char* func) {
    if (!UI_PRINT_ENABLED)
        return;
    fprintf(stderr, " ERROR: [%s:%d %s]\n", file, line, func);
}

void ui_print_creation_error_with_msg(const char* file, int line, const char* func, const char* msg) {
    if (!UI_PRINT_ENABLED)
        return;
    fprintf(stderr, " ERROR: [%s:%d %s] %s\n", file, line, func, msg);
}

// Backend printing functions - use client's color with informative backend messages
void ui_print_backend_init(w_client* client, const char* backend_name) {
    if (!UI_PRINT_ENABLED)
        return;
    const char* color = client_colors[client->client_number % NUM_COLORS];
    fprintf(stderr, "%sClient %4zu (active: %4zu, total: %4zu)\n\t  [%s] initialized%s\n", color, client->client_number,
            client->server->active_count, client->server->total_clients, backend_name, COLOR_RESET);
}

void ui_print_backend_state(w_client* client, const char* backend_name, const char* state_desc) {
    if (!UI_PRINT_ENABLED)
        return;
    const char* color = client_colors[client->client_number % NUM_COLORS];
    fprintf(stderr, "%s\t  [%s] %s%s\n", color, backend_name, state_desc, COLOR_RESET);
}

void ui_print_backend_error(w_client* client, const char* backend_name, const char* error_desc) {
    if (!UI_PRINT_ENABLED)
        return;
    const char* color = client_colors[client->client_number % NUM_COLORS];
    fprintf(stderr, "%s\t  [%s] ❌ ERROR: %s%s\n", color, backend_name, error_desc, COLOR_RESET);
}

void ui_print_backend_done(w_client* client, const char* backend_name) {
    if (!UI_PRINT_ENABLED)
        return;
    const char* color = client_colors[client->client_number % NUM_COLORS];
    fprintf(stderr, "%s\t  [%s] ✓ completed%s\n", color, backend_name, COLOR_RESET);
}

// Server printing functions
void ui_print_server_listen_error(const char* error) {
    if (!UI_PRINT_ENABLED)
        return;
    fprintf(stderr, "Server: Init of client failed: %s\n", error);
}

void ui_print_server_client_accept_error(const char* error) {
    if (!UI_PRINT_ENABLED)
        return;
    fprintf(stderr, "Server: Failed to create client task: %s\n", error);
}

void ui_print_server_listen_stopped(int fd) {
    if (!UI_PRINT_ENABLED)
        return;
    fprintf(stderr, "Server: Listening stopped on socket %d\n", fd);
}

void ui_print_server_init_error(const char* error) {
    if (!UI_PRINT_ENABLED)
        return;
    fprintf(stderr, "Server: Init failed: %s\n", error);
}
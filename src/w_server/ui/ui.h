#pragma once

#include "w_client.h"

// UI printing control flag - set to 1 to enable printing, 0 to disable
#define UI_PRINT_ENABLED 1

// ANSI color codes (48 colors ordered for maximum contrast and readability)
#define COLOR_RESET "\033[0m"
extern const char* client_colors[];
#define NUM_COLORS 48

// UI printing functions for w_client state machine
void ui_print_timeout(w_client* client, int timeout_sec);
void ui_print_read_error(w_client* client, const char* error);
void ui_print_connection_closed_by_client(w_client* client);
void ui_print_received_bytes(w_client* client, ssize_t bytes);
void ui_print_request_too_large(w_client* client);
void ui_print_bad_request(w_client* client);
void ui_print_request_details(w_client* client);
void ui_print_processing_request(w_client* client);
void ui_print_response_details(w_client* client, int code, const char* code_str, size_t response_len);
void ui_print_send_error(w_client* client, const char* error);
void ui_print_connection_closed_during_send(w_client* client);
void ui_print_unknown_state_error(w_client* client, int state);
void ui_print_creation_error(const char* file, int line, const char* func);
void ui_print_creation_error_with_msg(const char* file, int line, const char* func, const char* msg);
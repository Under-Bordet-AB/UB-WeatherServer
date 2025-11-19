#pragma once

#include <stddef.h>

// Simple HTTP GET client (replaces curl dependency)
typedef struct {
    char* buffer;
    size_t size;
    size_t capacity;
} http_response_t;

// Make an HTTP GET request to the specified URL
// Returns 0 on success, -1 on failure
// Response data is stored in the response struct
int http_get(const char* url, http_response_t* response);

// Initialize response struct
void http_response_init(http_response_t* response);

// Free response data
void http_response_cleanup(http_response_t* response);
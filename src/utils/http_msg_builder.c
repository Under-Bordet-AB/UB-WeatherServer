#include "http_msg_builder.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Internal helper: Calculate body length (handles NULL)
static size_t get_body_length(const char* body) {
    return body ? strlen(body) : 0;
}

// Internal helper: Build HTTP response with all components
static char* build_http_message(int status_code,
                                const char* status_text,
                                const char* content_type,
                                const char* body,
                                size_t body_length,
                                const char* extra_headers) {
    // Use provided content type or default to text/plain
    const char* ct = content_type ? content_type : "text/plain";

    // Calculate total size needed
    size_t size = snprintf(NULL, 0,
                           "HTTP/1.1 %d %s\r\n"
                           "Content-Type: %s\r\n"
                           "Content-Length: %zu\r\n"
                           "Connection: close\r\n",
                           status_code, status_text, ct, body_length);

    // Add extra headers size
    if (extra_headers) {
        size += strlen(extra_headers);
    }

    // Add header terminator and body
    size += 2; // \r\n
    size += body_length;
    size += 1; // null terminator

    // Allocate buffer
    char* response = malloc(size);
    if (!response) {
        return NULL;
    }

    // Build response
    int offset = sprintf(response,
                         "HTTP/1.1 %d %s\r\n"
                         "Content-Type: %s\r\n"
                         "Content-Length: %zu\r\n"
                         "Connection: close\r\n",
                         status_code, status_text, ct, body_length);

    // Add extra headers
    if (extra_headers) {
        offset += sprintf(response + offset, "%s", extra_headers);
    }

    // Add header terminator
    offset += sprintf(response + offset, "\r\n");

    // Add body if present
    if (body && body_length > 0) {
        memcpy(response + offset, body, body_length);
        offset += body_length;
    }

    response[offset] = '\0';
    return response;
}

// ============================================================================
// Core Response Builder
// ============================================================================

char* http_msg_build_response(int status_code,
                              const char* status_text,
                              const char* content_type,
                              const char* body,
                              const char* extra_headers) {
    size_t body_length = get_body_length(body);
    return build_http_message(status_code, status_text, content_type, body, body_length, extra_headers);
}

char* http_msg_build_binary_response(int status_code,
                                     const char* status_text,
                                     const char* content_type,
                                     const void* body,
                                     size_t body_size,
                                     const char* extra_headers) {
    return build_http_message(status_code, status_text, content_type, (const char*)body, body_size, extra_headers);
}

// ============================================================================
// Standard Success Responses (2xx)
// ============================================================================

char* http_msg_200_ok_json(const char* json_body) {
    const char* body = json_body ? json_body : "{}";
    return http_msg_build_response(200, "OK", "application/json", body, NULL);
}

char* http_msg_200_ok_text(const char* text_body) {
    const char* body = text_body ? text_body : "";
    return http_msg_build_response(200, "OK", "text/plain", body, NULL);
}

char* http_msg_200_ok_binary(const char* content_type, const void* data, size_t size) {
    const char* ct = content_type ? content_type : "application/octet-stream";
    return http_msg_build_binary_response(200, "OK", ct, data, size, NULL);
}

// ============================================================================
// Standard Client Error Responses (4xx)
// ============================================================================

char* http_msg_400_bad_request(const char* reason) {
    const char* body = reason ? reason : "Bad Request";
    return http_msg_build_response(400, "Bad Request", "text/plain", body, NULL);
}

char* http_msg_401_unauthorized(const char* realm) {
    const char* r = realm ? realm : "Restricted";
    char extra_headers[256];
    snprintf(extra_headers, sizeof(extra_headers), "WWW-Authenticate: Basic realm=\"%s\"\r\n", r);
    return http_msg_build_response(401, "Unauthorized", "text/plain", "Unauthorized", extra_headers);
}

char* http_msg_403_forbidden(const char* reason) {
    const char* body = reason ? reason : "Forbidden";
    return http_msg_build_response(403, "Forbidden", "text/plain", body, NULL);
}

char* http_msg_404_not_found(const char* resource) {
    char body[256];
    if (resource) {
        snprintf(body, sizeof(body), "Resource not found: %s", resource);
    } else {
        snprintf(body, sizeof(body), "Not Found");
    }
    return http_msg_build_response(404, "Not Found", "text/plain", body, NULL);
}

char* http_msg_405_method_not_allowed(const char* allowed_methods) {
    const char* methods = allowed_methods ? allowed_methods : "GET";
    char extra_headers[256];
    snprintf(extra_headers, sizeof(extra_headers), "Allow: %s\r\n", methods);
    return http_msg_build_response(405, "Method Not Allowed", "text/plain", "Method Not Allowed", extra_headers);
}

char* http_msg_408_request_timeout(void) {
    return http_msg_build_response(408, "Request Timeout", "text/plain", "Request Timeout", NULL);
}

char* http_msg_413_content_too_large(const char* max_size) {
    char body[256];
    if (max_size) {
        snprintf(body, sizeof(body), "Request too large. Maximum size: %s", max_size);
    } else {
        snprintf(body, sizeof(body), "Request Entity Too Large");
    }
    return http_msg_build_response(413, "Content Too Large", "text/plain", body, NULL);
}

char* http_msg_429_too_many_requests(int retry_after) {
    char* extra_headers = NULL;
    char extra_buf[64] = {0};

    if (retry_after > 0) {
        snprintf(extra_buf, sizeof(extra_buf), "Retry-After: %d\r\n", retry_after);
        extra_headers = extra_buf;
    }

    return http_msg_build_response(429, "Too Many Requests", "text/plain", "Too Many Requests", extra_headers);
}

// ============================================================================
// Standard Server Error Responses (5xx)
// ============================================================================

char* http_msg_500_internal_error(const char* details) {
    const char* body = details ? details : "Internal Server Error";
    return http_msg_build_response(500, "Internal Server Error", "text/plain", body, NULL);
}

char* http_msg_501_not_implemented(const char* feature) {
    char body[256];
    if (feature) {
        snprintf(body, sizeof(body), "Not Implemented: %s", feature);
    } else {
        snprintf(body, sizeof(body), "Not Implemented");
    }
    return http_msg_build_response(501, "Not Implemented", "text/plain", body, NULL);
}

char* http_msg_503_service_unavailable(int retry_after) {
    char* extra_headers = NULL;
    char extra_buf[64] = {0};

    if (retry_after > 0) {
        snprintf(extra_buf, sizeof(extra_buf), "Retry-After: %d\r\n", retry_after);
        extra_headers = extra_buf;
    }

    return http_msg_build_response(503, "Service Unavailable", "text/plain", "Service Unavailable", extra_headers);
}

// ============================================================================
// Utility Functions
// ============================================================================

size_t http_msg_get_total_size(const char* response) {
    return response ? strlen(response) : 0;
}

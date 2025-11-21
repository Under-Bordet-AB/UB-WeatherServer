#pragma once

#include <stddef.h>

/**
 * HTTP Message Builder
 *
 * Professional HTTP response construction utilities following RFC 7230-7235.
 * This module provides convenient functions to build standard HTTP responses
 * without requiring manual string construction.
 *
 * All functions return heap-allocated strings that must be freed by the caller.
 * All functions accept optional custom headers and body content.
 */

// ============================================================================
// Core Response Builder
// ============================================================================

/**
 * Build a complete HTTP response with custom status code
 *
 * @param status_code HTTP status code (e.g., 200, 404, 500)
 * @param status_text Status text (e.g., "OK", "Not Found")
 * @param content_type Content-Type header value (NULL for text/plain)
 * @param body Response body (NULL for no body)
 * @param extra_headers Additional headers as "Name: Value\r\n" pairs (NULL for none)
 * @return Heap-allocated complete HTTP response string, caller must free()
 */
char* http_msg_build_response(int status_code,
                              const char* status_text,
                              const char* content_type,
                              const char* body,
                              const char* extra_headers);

/**
 * Build a complete HTTP response for binary data
 *
 * @param status_code HTTP status code
 * @param status_text Status text
 * @param content_type Content-Type header value (e.g., "image/png")
 * @param body Binary data buffer
 * @param body_size Size of binary data in bytes
 * @param extra_headers Additional headers (NULL for none)
 * @return Heap-allocated complete HTTP response, caller must free()
 */
char* http_msg_build_binary_response(int status_code,
                                     const char* status_text,
                                     const char* content_type,
                                     const void* body,
                                     size_t body_size,
                                     const char* extra_headers);

// ============================================================================
// Standard Success Responses (2xx)
// ============================================================================

/**
 * Build 200 OK response with JSON content
 *
 * @param json_body JSON string (will be copied)
 * @return Heap-allocated HTTP response, caller must free()
 */
char* http_msg_200_ok_json(const char* json_body);

/**
 * Build 200 OK response with plain text content
 *
 * @param text_body Plain text string (will be copied)
 * @return Heap-allocated HTTP response, caller must free()
 */
char* http_msg_200_ok_text(const char* text_body);

/**
 * Build 200 OK response with binary content (e.g., images)
 *
 * @param content_type MIME type (e.g., "image/png", "application/octet-stream")
 * @param data Binary data buffer
 * @param size Size of binary data in bytes
 * @return Heap-allocated HTTP response, caller must free()
 */
char* http_msg_200_ok_binary(const char* content_type, const void* data, size_t size);

// ============================================================================
// Standard Client Error Responses (4xx)
// ============================================================================

/**
 * Build 400 Bad Request response
 *
 * @param reason Optional detailed error message (NULL for generic message)
 * @return Heap-allocated HTTP response, caller must free()
 */
char* http_msg_400_bad_request(const char* reason);

/**
 * Build 401 Unauthorized response
 *
 * @param realm Authentication realm for WWW-Authenticate header
 * @return Heap-allocated HTTP response, caller must free()
 */
char* http_msg_401_unauthorized(const char* realm);

/**
 * Build 403 Forbidden response
 *
 * @param reason Optional detailed error message (NULL for generic message)
 * @return Heap-allocated HTTP response, caller must free()
 */
char* http_msg_403_forbidden(const char* reason);

/**
 * Build 404 Not Found response
 *
 * @param resource Optional resource name that was not found (NULL for generic)
 * @return Heap-allocated HTTP response, caller must free()
 */
char* http_msg_404_not_found(const char* resource);

/**
 * Build 405 Method Not Allowed response
 *
 * @param allowed_methods Comma-separated list of allowed methods (e.g., "GET, POST")
 * @return Heap-allocated HTTP response, caller must free()
 */
char* http_msg_405_method_not_allowed(const char* allowed_methods);

/**
 * Build 408 Request Timeout response
 *
 * @return Heap-allocated HTTP response, caller must free()
 */
char* http_msg_408_request_timeout(void);

/**
 * Build 413 Content Too Large response
 *
 * @param max_size Optional max size string (NULL for generic message)
 * @return Heap-allocated HTTP response, caller must free()
 */
char* http_msg_413_content_too_large(const char* max_size);

/**
 * Build 429 Too Many Requests response
 *
 * @param retry_after Seconds to wait before retrying (0 for no Retry-After header)
 * @return Heap-allocated HTTP response, caller must free()
 */
char* http_msg_429_too_many_requests(int retry_after);

// ============================================================================
// Standard Server Error Responses (5xx)
// ============================================================================

/**
 * Build 500 Internal Server Error response
 *
 * @param details Optional error details (NULL for generic message)
 * @return Heap-allocated HTTP response, caller must free()
 */
char* http_msg_500_internal_error(const char* details);

/**
 * Build 501 Not Implemented response
 *
 * @param feature Optional feature name that is not implemented
 * @return Heap-allocated HTTP response, caller must free()
 */
char* http_msg_501_not_implemented(const char* feature);

/**
 * Build 503 Service Unavailable response
 *
 * @param retry_after Seconds to wait before retrying (0 for no Retry-After header)
 * @return Heap-allocated HTTP response, caller must free()
 */
char* http_msg_503_service_unavailable(int retry_after);

// ============================================================================
// Utility Functions
// ============================================================================

/**
 * Calculate total size needed for a response (for pre-allocation)
 * Useful for binary responses where you need to know exact size before sending
 *
 * @param response Response string returned by http_msg_* functions
 * @return Total size in bytes including headers and body
 */
size_t http_msg_get_total_size(const char* response);

#pragma once

#include "linked_list.h"

// HTTP/1.1 request/response parser
// Parses complete HTTP messages (not streaming)

#define HTTP_VERSION "HTTP/1.1"
#define MAX_URL_LEN 256

// A http_request struct should only be disposed by http_request_dispose

// Why parsing failed (if valid == 0)
typedef enum {
    INVALID_REASON_UNKNOWN = 0,
    INVALID_REASON_NOT_INVALID = 1, // Actually valid
    INVALID_REASON_MALFORMED = 2,
    INVALID_REASON_OUT_OF_MEMORY = 3,
    INVALID_REASON_URL_TOO_LONG =
        4 // Originally existed because the URL was fixed size in the struct, but kept for extra safety
} invalid_reason;

// HTTP methods supported
typedef enum {
    REQUEST_METHOD_UNKNOWN = 0,
    REQUEST_METHOD_GET = 1,
    REQUEST_METHOD_POST = 2,
} request_method;

// HTTP version - enum values match version numbers for easy display
typedef enum {
    PROTOCOL_VERSION_UNKNOWN = 0,
    PROTOCOL_VERSION_HTTP_0_9 = 9,  // Fixed: enum values now match version numbers (0.9 -> 9)
    PROTOCOL_VERSION_HTTP_1_0 = 10, // 1.0 -> 10
    PROTOCOL_VERSION_HTTP_1_1 = 11, // 1.1 -> 11
    PROTOCOL_VERSION_HTTP_2_0 = 20, // 2.0 -> 20
    PROTOCOL_VERSION_HTTP_3_0 = 30  // 3.0 -> 30
} protocol_version;

// Single HTTP header (stored in LinkedList)
typedef struct {
    const char* name;  // Heap-allocated, freed by free_header callback
    const char* value; // Heap-allocated, freed by free_header callback
} HTTPHeader;

typedef enum {
    RESPONSE_CODE_UNKNOWN = 0,
    RESPONSE_CODE_OK = 200,
    RESPONSE_CODE_MOVED_PERMANENTLY = 301,
    RESPONSE_CODE_FOUND = 302,
    RESPONSE_CODE_NOT_MODIFIED = 304,
    RESPONSE_CODE_TEMPORARY_REDIRECT = 307,
    RESPONSE_CODE_PERMANENT_REDIRECT = 308,
    RESPONSE_CODE_BAD_REQUEST = 400,
    RESPONSE_CODE_UNAUTHORIZED = 401,
    RESPONSE_CODE_FORBIDDEN = 403,
    RESPONSE_CODE_NOT_FOUND = 404,
    RESPONSE_CODE_METHOD_NOT_ALLOWED = 405,
    RESPONSE_CODE_REQUEST_TIMEOUT = 408,
    RESPONSE_CODE_GONE = 410,
    RESPONSE_CODE_LENGTH_REQUIRED = 411,
    RESPONSE_CODE_CONTENT_TOO_LARGE = 413,
    RESPONSE_CODE_URI_TOO_LONG = 414,
    RESPONSE_CODE_TOO_MANY_REQUESTS = 429,
    RESPONSE_CODE_INTERNAL_SERVER_ERROR = 500,
    RESPONSE_CODE_NOT_IMPLEMENTED = 501,
    RESPONSE_CODE_BAD_GATEWAY = 502,
    RESPONSE_CODE_SERVICE_UNAVAILABLE = 503,
    RESPONSE_CODE_GATEWAY_TIMEOUT = 504,
    RESPONSE_CODE_HTTP_VERSION_NOT_SUPPORTED = 505,
} response_code;

// Parsed HTTP request (from client)
typedef struct {
    int valid;             // 0 if parsing failed
    invalid_reason reason; // Why parsing failed (if valid == 0)
    request_method method;
    protocol_version protocol;
    char* url; // Heap-allocated, freed by http_request_dispose (JJ: removed const since i need to convert between
               // byte<->hex for saving proper filenames or sending over http))
    LinkedList* headers; // List of HTTPHeader*, freed by http_request_dispose
} http_request;

// Parsed HTTP response (from server)
typedef struct {
    int valid;             // 0 if parsing failed
    invalid_reason reason; // Why parsing failed (if valid == 0)
    response_code code;
    protocol_version protocol;
    LinkedList* headers; // List of HTTPHeader*, freed by http_response_dispose
    const char* body;    // Heap-allocated, freed by http_response_dispose
} http_response;

// === Request Functions ===

// Convert request_method enum to string like "GET"
const char* request_method_tostring(request_method method);

// Convert response_code enum to string like "OK"
const char* response_code_tostring(response_code code);

// Create new request - returns heap-allocated struct, must call http_request_dispose()
http_request* http_request_new(request_method method, const char* url);

// Add header to request - name and value are copied
int http_request_add_header(http_request* response, const char* name, const char* value);

// Serialize request to string - returns heap-allocated string, caller must free()
const char* http_request_tostring(http_request* request);

// Parse HTTP request from string - returns heap-allocated struct
http_request* http_request_fromstring(const char* request);

// Free request and set pointer to NULL - safe to call with NULL
void http_request_dispose(http_request** request);

// === Response Functions ===

// Create new response - returns heap-allocated struct, must call http_response_dispose()
http_response* http_response_new(response_code code, const char* body);

// Add header to response - name and value are copied
int http_response_add_header(http_response* response, const char* name, const char* value);

// Serialize response to string - returns heap-allocated string, caller must free()
const char* http_response_tostring(http_response* response);

// Parse HTTP response from string - returns heap-allocated struct
http_response* http_response_fromstring(const char* response);

// Free response and set pointer to NULL - safe to call with NULL
void http_response_dispose(http_response** response);

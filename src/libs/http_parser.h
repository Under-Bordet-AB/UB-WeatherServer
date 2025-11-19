#ifndef HTTP_PARSER_H
#define HTTP_PARSER_H

#define HTTP_VERSION "HTTP/1.1"
#define MAX_URL_LEN 256

#include "linked_list.h"

// A HTTPRequest struct should only be disposed by HTTPRequest_Dispose

// If a HTTPRequest is not valid, why?
typedef enum {
    INVALID_REASON_UNKNOWN = 0,
    INVALID_REASON_NOT_INVALID = 1,

    INVALID_REASON_MALFORMED = 2,
    INVALID_REASON_OUT_OF_MEMORY = 3,
    INVALID_REASON_URL_TOO_LONG = 4 // Originally existed because the URL was fixed size in the struct, but kept for extra safety
} InvalidReason;

typedef enum {
    REQUEST_METHOD_UNKNOWN = 0,

    REQUEST_METHOD_GET = 1,
    REQUEST_METHOD_POST = 2,
} RequestMethod;

typedef enum {
    PROTOCOL_VERSION_UNKNOWN = 0,

    PROTOCOL_VERSION_HTTP_0_9 = 9,  // Fixed: enum values now match version numbers (0.9 -> 9)
    PROTOCOL_VERSION_HTTP_1_0 = 10, // 1.0 -> 10
    PROTOCOL_VERSION_HTTP_1_1 = 11, // 1.1 -> 11
    PROTOCOL_VERSION_HTTP_2_0 = 20, // 2.0 -> 20
    PROTOCOL_VERSION_HTTP_3_0 = 30  // 3.0 -> 30
} ProtocolVersion;

typedef struct {
    const char* Name;
    const char* Value;
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
} ResponseCode;

// Serverside functions
typedef struct {
    int valid; // If false (0), then the request could not be parsed. Panic!
    InvalidReason reason;

    RequestMethod method;
    ProtocolVersion protocol;
    const char* URL;

    LinkedList* headers;
} HTTPRequest;

typedef struct {
    int valid; // If false (0), then the request could not be parsed. Panic!
    InvalidReason reason;

    ResponseCode responseCode;
    ProtocolVersion protocol;
    LinkedList* headers;
    const char* body;
} HTTPResponse;

const char* RequestMethod_tostring(RequestMethod method);

HTTPRequest* HTTPRequest_new(RequestMethod method, const char* URL);
int HTTPRequest_add_header(HTTPRequest* response, const char* name, const char* value);
const char* HTTPRequest_tostring(HTTPRequest* request);

HTTPRequest* HTTPRequest_fromstring(const char* request);
void HTTPRequest_Dispose(HTTPRequest** request);

HTTPResponse* HTTPResponse_new(ResponseCode code, const char* body);
int HTTPResponse_add_header(HTTPResponse* response, const char* name, const char* value);
const char* HTTPResponse_tostring(HTTPResponse* response);

HTTPResponse* HTTPResponse_fromstring(const char* response);
void HTTPResponse_Dispose(HTTPResponse** response);

#endif
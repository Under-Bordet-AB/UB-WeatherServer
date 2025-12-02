#ifndef HTTPPARSER_H
#define HTTPPARSER_H

/*
LIBRARY CONFIG
Important defines that control the behavior of the HTTPParser library.
For most use cases, the default values should work perfectly fine.
*/

// Prefer centralized G_ parser defines from `global_defines.h` when available.
#include "../global_defines.h"

// HTTP version
#ifndef HTTP_VERSION
#  ifdef G_HTTP_VERSION
#    define HTTP_VERSION G_HTTP_VERSION
#  else
#    define HTTP_VERSION "HTTP/1.1"
#  endif
#endif

// Auto-close connections
#ifndef CLOSE_CONNECTIONS
#  ifdef G_CLOSE_CONNECTIONS
#    define CLOSE_CONNECTIONS G_CLOSE_CONNECTIONS
#  else
#    define CLOSE_CONNECTIONS 1
#  endif
#endif

// URL length limit
#ifndef MAX_URL_LEN
#  ifdef G_MAX_URL_LEN
#    define MAX_URL_LEN G_MAX_URL_LEN
#  else
#    define MAX_URL_LEN 256
#  endif
#endif

// Strict validation
#ifndef STRICT_VALIDATION
#  ifdef G_STRICT_VALIDATION
#    define STRICT_VALIDATION G_STRICT_VALIDATION
#  else
#    define STRICT_VALIDATION 1
#  endif
#endif

// Variables to control preflight responses for CORS.
#ifndef CORS_ALLOWED_ORIGIN
#  ifdef G_CORS_ALLOWED_ORIGIN
#    define CORS_ALLOWED_ORIGIN G_CORS_ALLOWED_ORIGIN
#  else
#    define CORS_ALLOWED_ORIGIN "*"
#  endif
#endif

#ifndef CORS_ALLOWED_METHODS
#  ifdef G_CORS_ALLOWED_METHODS
#    define CORS_ALLOWED_METHODS G_CORS_ALLOWED_METHODS
#  else
#    define CORS_ALLOWED_METHODS "GET, OPTIONS"
#  endif
#endif

#ifndef CORS_ALLOWED_HEADERS
#  ifdef G_CORS_ALLOWED_HEADERS
#    define CORS_ALLOWED_HEADERS G_CORS_ALLOWED_HEADERS
#  else
#    define CORS_ALLOWED_HEADERS ""
#  endif
#endif
// (not required for Accept, Accept-Language, Content-Language or Content-Type for form data or text/plain)

/*
          BEGIN LIBRARY CODE
*/

#include "linked_list.h"
#include <stdint.h>

// HTTPQuery - Path & query separator for URLs

typedef struct {
    const char* Name;
    const char* Value;
} HTTPQueryParameter;
typedef struct {
    const char* Path;
    LinkedList* Query;
} HTTPQuery;

HTTPQuery* HTTPQuery_fromstring(const char* URL);
const char* HTTPQuery_getParameter(HTTPQuery* query, const char* name); // Returns GET parameter value if name found, NULL if not found
void HTTPQuery_Dispose(HTTPQuery** query);

// A HTTPRequest struct should only be disposed by HTTPRequest_Dispose

// If a HTTPRequest is not valid, why?
typedef enum {
    Unknown = 0,
    NotInvalid = 1,

    Malformed = 2,
    OutOfMemory = 3,
    URLTooLong = 4, // Originally existed because the URL was fixed size in the struct, but kept for extra safety
    InvalidMethod = 5, // only for STRICT_VALIDATION
    InvalidProtocol = 6, // only for STRICT_VALIDATION
    InvalidURL = 7 // URLs must begin with a slash
} InvalidReason;

const char* InvalidReason_tostring(InvalidReason method);

typedef enum {
    Method_Unknown = 0,

    GET = 1,
    POST = 2,
    PUT = 3,
    DELETE = 4,
    PATCH = 5,
    OPTIONS = 6,
    HEAD = 7
} RequestMethod;

typedef enum {
    Protocol_Unknown = 0,

    HTTP_0_9 = 1,
    HTTP_1_0 = 2,
    HTTP_1_1 = 3,
    HTTP_2_0 = 4,
    HTTP_3_0 = 5
} ProtocolVersion;

typedef struct {
    const char* Name;
    const char* Value;
} HTTPHeader;

typedef enum {
    ResponseCode_Unknown = 0,

    OK = 200,
    No_Content = 204,

    Moved_Permanently = 301,
    Found = 302,
    Not_Modified = 304,
    Temporary_Redirect = 307,
    Permanent_Redirect = 308,

    Bad_Request = 400,
    Unauthorized = 401,
    Forbidden = 403,
    Not_Found = 404,
    Method_Not_Allowed = 405,
    Request_Timeout = 408,
    Gone = 410,
    Length_Required = 411,
    Content_Too_Large = 413,
    URI_Too_Long = 414,
    Too_Many_Requests = 429,
    Request_Header_Fields_Too_Large = 431,

    Internal_Server_Error = 500,
    Not_Implemented = 501,
    Bad_Gateway = 502,
    Service_Unavailable = 503,
    Gateway_Timeout = 504,
    HTTP_Version_Not_Supported = 505,
} ResponseCode;

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

    uint8_t* body;
    size_t bodySize;
} HTTPResponse;

const char* RequestMethod_tostring(RequestMethod method);

HTTPRequest* HTTPRequest_new(RequestMethod method, const char* URL);
int HTTPRequest_add_header(HTTPRequest* response, const char* name, const char* value);
const char* HTTPRequest_tostring(HTTPRequest* request);
HTTPRequest* HTTPRequest_fromstring(const char* request); // DEPRECATED: USE HTTPRequestParser
void HTTPRequest_Dispose(HTTPRequest** request);

HTTPResponse* HTTPResponse_new(ResponseCode code, uint8_t* body, size_t bodyLength);
int HTTPResponse_add_header(HTTPResponse* response, const char* name, const char* value);
const char* HTTPResponse_tostring(HTTPResponse* response, size_t* outSize);
HTTPResponse* HTTPResponse_fromstring(const char* response);
void HTTPResponse_Dispose(HTTPResponse** response);

#endif
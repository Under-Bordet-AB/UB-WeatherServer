#define _POSIX_C_SOURCE 200809L

#include "http_parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Extract substring from start to end pointers
char* substr(const char* start, const char* end) {
    if (!start || !end || end < start) {
        return NULL;
    }
    size_t len = end - start;
    char* out = malloc(len + 1);
    if (!out) {
        return NULL;
    }
    memcpy(out, start, len);
    out[len] = '\0';
    return out;
}

// Convert string to request_method enum
request_method Enum_Method(const char* method) {
    if (!method) {
        return REQUEST_METHOD_UNKNOWN;
    }

    if (strcmp(method, "GET") == 0) {
        return REQUEST_METHOD_GET;
    }
    if (strcmp(method, "POST") == 0) {
        return REQUEST_METHOD_POST;
    }

    return REQUEST_METHOD_UNKNOWN;
}

// Convert string like "HTTP/1.1" to protocol_version enum
protocol_version Enum_Protocol(const char* protocol) {
    if (!protocol) {
        return PROTOCOL_VERSION_UNKNOWN;
    }
    if (strcmp(protocol, "HTTP/0.9") == 0) {
        return PROTOCOL_VERSION_HTTP_0_9;
    }
    if (strcmp(protocol, "HTTP/1.0") == 0) {
        return PROTOCOL_VERSION_HTTP_1_0;
    }
    if (strcmp(protocol, "HTTP/1.1") == 0) {
        return PROTOCOL_VERSION_HTTP_1_1;
    }
    if (strcmp(protocol, "HTTP/2.0") == 0) {
        return PROTOCOL_VERSION_HTTP_2_0;
    }
    if (strcmp(protocol, "HTTP/3.0") == 0) {
        return PROTOCOL_VERSION_HTTP_3_0;
    }

    return PROTOCOL_VERSION_UNKNOWN;
}

// Callback for LinkedList_dispose - frees header name, value, and struct itself
void free_header(void* context) {
    HTTPHeader* hdr = (HTTPHeader*)context;
    free((void*)hdr->name);
    free((void*)hdr->value);
    // BUG FIX (2025-11-19): Original code was missing free(hdr), causing memory leak
    // of HTTPHeader structs (16 bytes each). LinkedList_dispose calls this callback
    // expecting it to free the entire item, not just internal fields.
    free(hdr);
}

const char* request_method_tostring(request_method method) {
    switch (method) {
    case REQUEST_METHOD_GET:
        return "GET";
    case REQUEST_METHOD_POST:
        return "POST";
    default:
        return "GET";
    }
}

// Map response code number to human-readable message
const char* response_code_tostring(response_code code) {
    switch (code) {
    case 200:
        return "OK";
    case 301:
        return "Moved Permanently";
    case 302:
        return "Found";
    case 304:
        return "Not Modified";
    case 400:
        return "Bad Request";
    case 401:
        return "Unauthorized";
    case 403:
        return "Forbidden";
    case 404:
        return "Not Found";
    case 405:
        return "Method Not Allowed";
    case 500:
        return "Internal Server Error";
    case 501:
        return "Not Implemented";
    case 503:
        return "Service Unavailable";
    default:
        return "";
    }
}

// Parse string to integer, returns -1 on failure
int parseInt(const char* str) {
    char* end;
    long val = strtol(str, &end, 10);
    if (*str == '\0' || *end != '\0') {
        return -1;
    }
    return (int)val;
}

// Create new HTTP request - caller must dispose with http_request_dispose()
http_request* http_request_new(request_method method, const char* url) {
    http_request* request = calloc(1, sizeof(http_request));
    request->method = method;
    request->url = strdup(url); // Allocate copy
    request->headers = LinkedList_create();
    return request;
}

int http_request_add_header(http_request* request, const char* name, const char* value) {
    if (request->headers == NULL) {
        return 0;
    }
    HTTPHeader* header = calloc(1, sizeof(HTTPHeader));
    if (header == NULL) {
        return 0;
    }
    header->name = strdup(name);
    if (header->name == NULL) {
        return 0;
    }
    header->value = strdup(value);
    if (header->value == NULL) {
        return 0;
    }
    return LinkedList_append(request->headers, header);
}

// Serialize request to string - caller must free() returned memory
const char* http_request_tostring(http_request* request) {
    const char* method = request_method_tostring(request->method);
    // Calculate total size needed
    int message_size = 2 + strlen(method) + strlen(HTTP_VERSION) + strlen(request->url);
    if (request->headers != NULL) {
        LinkedList_foreach(request->headers, node) {
            HTTPHeader* hdr = (HTTPHeader*)node->item;
            message_size += 4 + strlen(hdr->name) + strlen(hdr->value); // "\r\nName: Value"
        }
    }
    message_size += 4; // Final "\r\n\r\n"
    char* status = malloc(message_size);
    // Write status line: "GET /path HTTP/1.1"
    int cur_pos = snprintf(status, message_size, "%s %s %s", method, request->url, HTTP_VERSION);
    // Write each header
    LinkedList_foreach(request->headers, node) {
        HTTPHeader* hdr = (HTTPHeader*)node->item;
        int written = snprintf(&status[cur_pos], message_size - cur_pos, "\r\n%s: %s", hdr->name, hdr->value);
        cur_pos += written;
    }
    // Terminate with blank line
    snprintf(&status[cur_pos], message_size - cur_pos, "\r\n\r\n");
    return status;
}

// Parse a request from Client -> Server
http_request* http_request_fromstring(const char* message) {
    http_request* request = calloc(1, sizeof(http_request));
    request->reason = INVALID_REASON_MALFORMED; // Default to error, clear on success
    request->headers = LinkedList_create();

    int state = 0; // 0=parsing status line, 1=parsing headers

    const char* start = message;
    int final_loop = 0;
    while (start && *start && !final_loop) {
        // Find end of current line
        const char* end = strstr(start, "\r\n");
        if (!end) {
            // No \r\n found, process remaining text and exit
            final_loop = 1;
            end = message + strlen(message);
        }
        // Calculate line length
        int length = end - start;
        if (length < 2) {
            // Empty or too short, we're done parsing headers
            break;
        }
        // Extract current line
        char* current_line = substr(start, end);
        if (!current_line) { // Out of memory
            break;
        }

        if (state == 0) {
            // Parse status line: "GET /path HTTP/1.1"
            // Should have exactly 2 spaces
            int count = 0;
            char* scan = current_line;
            for (; *scan; scan++) {
                if (*scan == ' ') {
                    count++;
                }
            }
            if (count != 2) {
                printf("INVALID: Request is not formatted with 2 spaces.\n\n");
                free(current_line);
                break;
            }

            // Find space positions to split "METHOD URL PROTOCOL"
            const char* space1 = strchr(current_line, ' ');
            const char* space2 = strchr(space1 + 1, ' ');

            // Check URL length
            if (space2 - (space1 + 1) >= MAX_URL_LEN) {
                printf("INVALID: Request URL is too long\n\n");
                request->reason = INVALID_REASON_URL_TOO_LONG;
                free(current_line);
                break;
            }

            // Extract method, path, protocol
            char* method = substr(current_line, space1);
            if (!method) {
                free(current_line);
                request->reason = INVALID_REASON_OUT_OF_MEMORY;
                break;
            }
            char* path = substr(space1 + 1, space2);
            if (!path) {
                free(current_line);
                request->reason = INVALID_REASON_OUT_OF_MEMORY;
                break;
            }
            char* protocol = substr(space2 + 1, current_line + length);
            if (!protocol) {
                free(current_line);
                request->reason = INVALID_REASON_OUT_OF_MEMORY;
                break;
            }

            // Convert strings to enums and store
            request->method = Enum_Method(method);
            request->protocol = Enum_Protocol(protocol);
            request->url = path; // Keep path, free others

            free(method);
            free(protocol);

            request->valid = 1;
            request->reason = INVALID_REASON_NOT_INVALID;
            state = 1; // Switch to header parsing
        } else {
            // Parse header line: "Name: Value"
            const char* sep = strstr(current_line, ": ");
            if (!sep) {
                printf("INVALID: Header is malformed.\n\n");
                free(current_line);
                break;
            }

            // Extract name and value
            char* name = substr(current_line, sep);
            if (!name) {
                free(current_line);
                break;
            }
            char* value = substr(sep + 2, current_line + length); // +2 to skip ": "
            if (!value) {
                free(current_line);
                break;
            }

            // Create header and add to list
            HTTPHeader* header = calloc(1, sizeof(HTTPHeader));
            if (header != NULL) {
                header->name = name;
                header->value = value;
                LinkedList_append(request->headers, header);
            }
        }

        // Move to next line
        start = end + 2; // Skip "\r\n"
        free(current_line);
    }

    return request;
}

// Free request and all allocated memory, sets pointer to NULL
void http_request_dispose(http_request** req) {
    if (req && *req) {
        http_request* request = *req;
        free((void*)request->url);
        LinkedList_dispose(&request->headers, free_header);
        free(request);
        *req = NULL;
    }
}

// Create new HTTP response - caller must dispose with http_response_dispose()
http_response* http_response_new(response_code code, const char* body) {
    http_response* response = calloc(1, sizeof(http_response));
    response->code = code;
    response->body = strdup(body); // Allocate copy
    response->headers = LinkedList_create();

    return response;
}

int http_response_add_header(http_response* response, const char* name, const char* value) {
    if (response->headers == NULL) {
        return 0;
    }
    HTTPHeader* header = calloc(1, sizeof(HTTPHeader));
    if (header == NULL) {
        return 0;
    }
    header->name = strdup(name);
    if (header->name == NULL) {
        return 0;
    }
    header->value = strdup(value);
    if (header->value == NULL) {
        return 0;
    }
    return LinkedList_append(response->headers, header);
}

// Serialize response to string - caller must free() returned memory
const char* http_response_tostring(http_response* response) {
    const char* message = response_code_tostring(response->code);
    // Calculate total size needed
    // 5 = 2 spaces + response code (3 digits) + null term
    int message_size = 6 + strlen(HTTP_VERSION) + strlen(message);
    if (response->headers != NULL) {
        LinkedList_foreach(response->headers, node) {
            HTTPHeader* hdr = (HTTPHeader*)node->item;
            message_size += 4 + strlen(hdr->name) + strlen(hdr->value); // "\r\nName: Value"
        }
    }
    message_size += 4 + strlen(response->body); // Final "\r\n\r\n" + body
    char* status = malloc(message_size);
    // Write status line: "HTTP/1.1 200 OK"
    int cur_pos = snprintf(status, message_size, "%s %d %s", HTTP_VERSION, response->code, message);
    // Write each header
    LinkedList_foreach(response->headers, node) {
        HTTPHeader* hdr = (HTTPHeader*)node->item;
        int written = snprintf(&status[cur_pos], message_size - cur_pos, "\r\n%s: %s", hdr->name, hdr->value);
        cur_pos += written;
    }
    // Write blank line and body
    snprintf(&status[cur_pos], message_size - cur_pos, "\r\n\r\n%s", response->body);
    return status;
}

// Parse a response from Server -> Client
http_response* http_response_fromstring(const char* message) {
    http_response* response = calloc(1, sizeof(http_response));
    response->reason = INVALID_REASON_MALFORMED; // Default to error
    response->headers = LinkedList_create();

    int message_len = strlen(message);
    int state = 0; // 0=parsing status line, 1=parsing headers

    const char* start = message;
    int final_loop = 0;
    while (start && *start && !final_loop) {
        const char* end = strstr(start, "\r\n");
        if (!end) {
            final_loop = 1;
            end = message + message_len;
        }
        int length = end - start;
        if (length < 2) {
            // Blank line signals end of headers, rest is body
            response->body = substr(start + 2, message + message_len);
            break;
        }
        char* current_line = substr(start, end);
        if (!current_line) { // Out of memory
            break;
        }

        if (state == 0) {
            // Parse status line: "HTTP/1.1 200 OK"
            // Should have exactly 2 spaces
            int count = 0;
            char* scan = current_line;
            for (; *scan; scan++) {
                if (*scan == ' ') {
                    count++;
                }
            }
            if (count != 2) {
                printf("INVALID: Response is not formatted with 2 spaces.\n\n");
                free(current_line);
                break;
            }

            // Find space positions to split "PROTOCOL CODE MESSAGE"
            const char* space1 = strchr(current_line, ' ');
            const char* space2 = strchr(space1 + 1, ' ');

            // Extract protocol and code
            char* protocol = substr(current_line, space1);
            if (!protocol) {
                free(current_line);
                response->reason = INVALID_REASON_OUT_OF_MEMORY;
                break;
            }
            char* code = substr(space1 + 1, space2);
            if (!code) {
                free(current_line);
                response->reason = INVALID_REASON_OUT_OF_MEMORY;
                break;
            }

            // Convert code string to integer
            int code_as_integer = parseInt(code);
            if (code_as_integer == -1) {
                printf("INVALID: Non-numeric response code.\n\n");
                free(current_line);
                break;
            }
            response->code = code_as_integer;
            response->protocol = Enum_Protocol(protocol);

            free(code);
            free(protocol);

            response->valid = 1;
            response->reason = INVALID_REASON_NOT_INVALID;
            state = 1; // Switch to header parsing
        } else {
            // Parse header line: "Name: Value"
            const char* sep = strstr(current_line, ": ");
            if (!sep) {
                printf("INVALID: Header is malformed.\n\n");
                free(current_line);
                break;
            }

            // Extract name and value
            char* name = substr(current_line, sep);
            if (!name) {
                free(current_line);
                break;
            }
            char* value = substr(sep + 2, current_line + length); // +2 to skip ": "
            if (!value) {
                free(current_line);
                break;
            }

            // Create header and add to list
            HTTPHeader* header = calloc(1, sizeof(HTTPHeader));
            if (header != NULL) {
                header->name = name;
                header->value = value;
                LinkedList_append(response->headers, header);
            }
        }

        // Move to next line
        start = end + 2; // Skip "\r\n"
        free(current_line);
    }

    return response;
}

// Free response and all allocated memory, sets pointer to NULL
void http_response_dispose(http_response** resp) {
    if (resp && *resp) {
        http_response* response = *resp;
        free((void*)response->body);
        LinkedList_dispose(&response->headers, free_header);
        free(response);
        *resp = NULL;
    }
}
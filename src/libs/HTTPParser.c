#define _POSIX_C_SOURCE 200809L

#include "HTTPParser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char* substr(const char* start, const char* end) {
    if (!start || !end || end < start) return NULL;
    size_t len = end - start;
    char* out = malloc(len + 1);
    if (!out) return NULL;
    memcpy(out, start, len);
    out[len] = '\0';
    return out;
}

/*
char* strfind(const char* str, const char* match) {
    size_t matchLen = strlen(match);
    if (matchLen == 0) return NULL;
    for (; *str; str++) {
        if (strncmp(str, match, matchLen) == 0)
            return (char*)str;
    }
    return NULL;
}
*/

#define strfind strstr

RequestMethod Enum_Method(const char* method) {
    if (!method) return Method_Unknown;

    if (strcmp(method, "GET") == 0) return GET;
    if (strcmp(method, "POST") == 0) return POST;

    return Method_Unknown;
}

ProtocolVersion Enum_Protocol(const char* protocol) {
    if (!protocol) return Protocol_Unknown;

    if (strcmp(protocol, "HTTP/0.9") == 0) return HTTP_0_9;
    if (strcmp(protocol, "HTTP/1.0") == 0) return HTTP_1_0;
    if (strcmp(protocol, "HTTP/1.1") == 0) return HTTP_1_1;
    if (strcmp(protocol, "HTTP/2.0") == 0) return HTTP_2_0;
    if (strcmp(protocol, "HTTP/3.0") == 0) return HTTP_3_0;

    return Protocol_Unknown;
}

void free_header(void* context) {
    HTTPHeader* hdr = (HTTPHeader*)context;
    free((void*)hdr->Name);
    free((void*)hdr->Value);
}

const char* RequestMethod_tostring(RequestMethod method) {
    switch (method) {
    case GET:
        return "GET";
    case POST:
        return "POST";
    default:
        return "GET";
    }
}

const char* CommonResponseMessages(ResponseCode code) {
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

int parseInt(const char* str) {
    char* end;
    long val = strtol(str, &end, 10);
    if (*str == '\0' || *end != '\0') { return -1; }
    return (int)val;
}

HTTPRequest* HTTPRequest_new(RequestMethod method, const char* URL) {
    HTTPRequest* request = calloc(1, sizeof(HTTPRequest));
    request->method = method;
    request->URL = strdup(URL);
    request->headers = LinkedList_create();

    return request;
}

int HTTPRequest_add_header(HTTPRequest* request, const char* name, const char* value) {
    if (request->headers == NULL) return 0;
    HTTPHeader* header = calloc(1, sizeof(HTTPHeader));
    if (header == NULL) return 0;
    header->Name = strdup(name);
    if (header->Name == NULL) return 0;
    header->Value = strdup(value);
    if (header->Value == NULL) return 0;
    return LinkedList_append(request->headers, header);
}

const char* HTTPRequest_tostring(HTTPRequest* request) {
    const char* method = RequestMethod_tostring(request->method);
    int messageSize = 2 + strlen(method) + strlen(HTTP_VERSION) + strlen(request->URL);
    if (request->headers != NULL) {
        LinkedList_foreach(request->headers, node) {
            HTTPHeader* hdr = (HTTPHeader*)node->item;
            messageSize += 4 + strlen(hdr->Name) + strlen(hdr->Value);
        }
    }
    messageSize += 4; // + strlen(request->body);
    char* status = malloc(messageSize);
    // write first line
    int curPos = snprintf(status, messageSize, "%s %s %s", method, request->URL, HTTP_VERSION);
    // write headers
    LinkedList_foreach(request->headers, node) {
        HTTPHeader* hdr = (HTTPHeader*)node->item;
        int written = snprintf(&status[curPos], messageSize - curPos, "\r\n%s: %s", hdr->Name, hdr->Value);
        curPos += written;
    }
    // write body
    snprintf(&status[curPos], messageSize - curPos, "\r\n\r\n");
    return status;
}

// Parse a request from Client -> Server
HTTPRequest* HTTPRequest_fromstring(const char* message) {
    HTTPRequest* request = calloc(1, sizeof(HTTPRequest));
    request->reason = Malformed;
    request->headers = LinkedList_create();

    int state = 0;

    const char* start = message;
    int finalLoop = 0;
    while (start && *start && !finalLoop) {
        // Find end of line
        const char* end = strstr(start, "\r\n");
        if (!end) {
            // No separator found, read to end of message and do not loop again
            finalLoop = 1;
            end = message + strlen(message);
        }
        // Check length with pointer math
        int length = end - start;
        if (length < 2) {
            // printf("Reached end of request.\n\n");
            break;
        }
        // Allocate memory for current line
        char* current_line = substr(start, end);
        if (!current_line) // horrible problem
            break;

        if (state == 0) {
            // Count spaces in request, should match 2
            int count = 0;
            char* scan = current_line;
            for (; *scan; scan++) {
                if (*scan == ' ') count++;
            }
            if (count != 2) {
                printf("INVALID: Request is not formatted with 2 spaces.\n\n");
                free(current_line);
                break;
            }

            const char* space1 = strchr(current_line, ' ');
            const char* space2 = strchr(space1 + 1, ' ');

            if (space2 - (space1 + 1) >= MAX_URL_LEN) {
                printf("INVALID: Request URL is too long\n\n");
                request->reason = URLTooLong;
                free(current_line);
                break;
            }

            char* method = substr(current_line, space1);
            if (!method) {
                free(current_line);
                request->reason = OutOfMemory;
                break;
            }
            char* path = substr(space1 + 1, space2);
            if (!path) {
                free(current_line);
                request->reason = OutOfMemory;
                break;
            }
            char* protocol = substr(space2 + 1, current_line + length);
            if (!protocol) {
                free(current_line);
                request->reason = OutOfMemory;
                break;
            }

            request->method = Enum_Method(method);
            request->protocol = Enum_Protocol(protocol);
            request->URL = path;

            free(method);
            free(protocol);

            request->valid = 1;
            request->reason = NotInvalid;
            state = 1; // jump to header parsing
        } else {
            const char* sep = strfind(current_line, ": ");
            if (!sep) {
                printf("INVALID: Header is malformed.\n\n");
                free(current_line);
                break;
            }

            char* name = substr(current_line, sep);
            if (!name) {
                free(current_line);
                break;
            }
            char* value = substr(sep + 2, current_line + length);
            if (!value) {
                free(current_line);
                break;
            }

            HTTPHeader* header = calloc(1, sizeof(HTTPHeader));
            if (header != NULL) {
                header->Name = name;
                header->Value = value;
                LinkedList_append(request->headers, header);
            }
        }

        start = end + 2;
        free(current_line);
    }

    return request;
}

// Properly dispose a HTTPRequest struct
void HTTPRequest_Dispose(HTTPRequest** req) {
    if (req && *req) {
        HTTPRequest* request = *req;
        free((void*)request->URL);
        LinkedList_dispose(&request->headers, free_header);
        free(request);
        *req = NULL;
    }
}

HTTPResponse* HTTPResponse_new(ResponseCode code, const char* body) {
    HTTPResponse* response = calloc(1, sizeof(HTTPResponse));
    response->responseCode = code;
    response->body = strdup(body);
    response->headers = LinkedList_create();

    return response;
}

int HTTPResponse_add_header(HTTPResponse* response, const char* name, const char* value) {
    if (response->headers == NULL) return 0;
    HTTPHeader* header = calloc(1, sizeof(HTTPHeader));
    if (header == NULL) return 0;
    header->Name = strdup(name);
    if (header->Name == NULL) return 0;
    header->Value = strdup(value);
    if (header->Value == NULL) return 0;
    return LinkedList_append(response->headers, header);
}

const char* HTTPResponse_tostring(HTTPResponse* response) {
    const char* message = CommonResponseMessages(response->responseCode);
    // Count size of everything before allocating
    // 5 = 2 spaces + response code (3 digits) + null term
    int messageSize = 6 + strlen(HTTP_VERSION) + strlen(message);
    if (response->headers != NULL) {
        LinkedList_foreach(response->headers, node) {
            HTTPHeader* hdr = (HTTPHeader*)node->item;
            messageSize += 4 + strlen(hdr->Name) + strlen(hdr->Value); // 4 = \r\n and symbols between name & value
        }
    }
    messageSize += 4 + strlen(response->body); // 4 = \r\n\r\n
    // printf("We have to allocate %i bytes.\n",messageSize);
    char* status = malloc(messageSize);
    // write first line
    int curPos = snprintf(status, messageSize, "%s %d %s", HTTP_VERSION, response->responseCode, message);
    // write headers
    LinkedList_foreach(response->headers, node) {
        HTTPHeader* hdr = (HTTPHeader*)node->item;
        int written = snprintf(&status[curPos], messageSize - curPos, "\r\n%s: %s", hdr->Name, hdr->Value);
        curPos += written;
    }
    // write body
    snprintf(&status[curPos], messageSize - curPos, "\r\n\r\n%s", response->body);
    return status;
}

// Parse a response from Server -> Client
HTTPResponse* HTTPResponse_fromstring(const char* message) {
    HTTPResponse* response = calloc(1, sizeof(HTTPResponse));
    response->reason = Malformed;
    response->headers = LinkedList_create();

    int messageLen = strlen(message);
    int state = 0;

    const char* start = message;
    int finalLoop = 0;
    while (start && *start && !finalLoop) {
        const char* end = strstr(start, "\r\n");
        if (!end) {
            finalLoop = 1;
            end = message + messageLen;
        }
        int length = end - start;
        if (length < 2) {
            // printf("Reached end of response.\n");
            response->body = substr(start + 2, message + messageLen);
            break;
        }
        char* current_line = substr(start, end);
        if (!current_line) // horrible problem
            break;

        if (state == 0) {
            int count = 0;
            char* scan = current_line;
            for (; *scan; scan++) {
                if (*scan == ' ') count++;
            }
            if (count != 2) {
                printf("INVALID: Response is not formatted with 2 spaces.\n\n");
                free(current_line);
                break;
            }

            const char* space1 = strchr(current_line, ' ');
            const char* space2 = strchr(space1 + 1, ' ');

            char* protocol = substr(current_line, space1);
            if (!protocol) {
                free(current_line);
                response->reason = OutOfMemory;
                break;
            }
            char* code = substr(space1 + 1, space2);
            if (!code) {
                free(current_line);
                response->reason = OutOfMemory;
                break;
            }

            int codeAsInteger = parseInt(code);
            if (codeAsInteger == -1) {
                printf("INVALID: Non-numeric response code.\n\n");
                free(current_line);
                break;
            }
            response->responseCode = codeAsInteger;
            response->protocol = Enum_Protocol(protocol);

            free(code);
            free(protocol);

            response->valid = 1;
            response->reason = NotInvalid;
            state = 1;
        } else {
            const char* sep = strfind(current_line, ": ");
            if (!sep) {
                printf("INVALID: Header is malformed.\n\n");
                free(current_line);
                break;
            }

            char* name = substr(current_line, sep);
            if (!name) {
                free(current_line);
                break;
            }
            char* value = substr(sep + 2, current_line + length);
            if (!value) {
                free(current_line);
                break;
            }

            HTTPHeader* header = calloc(1, sizeof(HTTPHeader));
            if (header != NULL) {
                header->Name = name;
                header->Value = value;
                LinkedList_append(response->headers, header);
            }
        }

        start = end + 2;
        free(current_line);
    }

    return response;
}

void HTTPResponse_Dispose(HTTPResponse** resp) {
    if (resp && *resp) {
        HTTPResponse* response = *resp;
        free((void*)response->body);
        LinkedList_dispose(&response->headers, free_header);
        free(response);
        *resp = NULL;
    }
}
#include "http_client.h"

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <unistd.h>

#define BUFFER_SIZE 4096
#define DEFAULT_PORT 80
#define HTTPS_PORT 443

// Simple URL parsing
typedef struct {
    char* host;
    char* path;
    int port;
    int is_https;
} parsed_url_t;

static int parse_url(const char* url, parsed_url_t* parsed) {
    if (!url || !parsed)
        return -1;

    memset(parsed, 0, sizeof(parsed_url_t));

    // Check for https
    if (strncmp(url, "https://", 8) == 0) {
        parsed->is_https = 1;
        parsed->port = HTTPS_PORT;
        url += 8;
    } else if (strncmp(url, "http://", 7) == 0) {
        parsed->is_https = 0;
        parsed->port = DEFAULT_PORT;
        url += 7;
    } else {
        return -1; // Invalid URL
    }

    // Find host and path
    const char* path_start = strchr(url, '/');
    if (path_start) {
        parsed->host = strndup(url, path_start - url);
        parsed->path = strdup(path_start);
    } else {
        parsed->host = strdup(url);
        parsed->path = strdup("/");
    }

    // Check for custom port
    char* colon = strchr(parsed->host, ':');
    if (colon) {
        *colon = '\0';
        parsed->port = atoi(colon + 1);
    }

    return 0;
}

static void free_parsed_url(parsed_url_t* parsed) {
    free(parsed->host);
    free(parsed->path);
}

static int connect_to_host(const char* host, int port) {
    // TODO blocking call to netdb.h
    struct hostent* server = gethostbyname(host);
    if (!server)
        return -1;

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
        return -1;

    // Set socket timeouts to prevent blocking the event loop
    struct timeval timeout;
    timeout.tv_sec = 2; // 2 second timeout
    timeout.tv_usec = 0;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    memcpy(&server_addr.sin_addr.s_addr, server->h_addr_list[0], server->h_length);
    server_addr.sin_port = htons(port);

    if (connect(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        close(sockfd);
        return -1;
    }

    return sockfd;
}

void http_response_init(http_response_t* response) {
    if (!response)
        return;
    memset(response, 0, sizeof(http_response_t));
}

void http_response_cleanup(http_response_t* response) {
    if (!response)
        return;
    free(response->buffer);
    response->buffer = NULL;
    response->size = 0;
    response->capacity = 0;
}

static int append_to_response(http_response_t* response, const char* data, size_t len) {
    if (!response || !data)
        return -1;

    size_t new_size = response->size + len;
    if (new_size >= response->capacity) {
        size_t new_capacity = response->capacity == 0 ? 1024 : response->capacity * 2;
        while (new_capacity < new_size + 1) {
            new_capacity *= 2;
        }

        char* new_buffer = realloc(response->buffer, new_capacity);
        if (!new_buffer)
            return -1;

        response->buffer = new_buffer;
        response->capacity = new_capacity;
    }

    memcpy(response->buffer + response->size, data, len);
    response->size = new_size;
    response->buffer[response->size] = '\0';

    return 0;
}

int http_get(const char* url, http_response_t* response) {
    if (!url || !response)
        return -1;

    parsed_url_t parsed;
    if (parse_url(url, &parsed) != 0) {
        return -1;
    }

    // HTTPS not supported in this simple implementation
    if (parsed.is_https) {
        free_parsed_url(&parsed);
        return -1;
    }

    int sockfd = connect_to_host(parsed.host, parsed.port);
    if (sockfd < 0) {
        free_parsed_url(&parsed);
        return -1;
    }

    // Send HTTP GET request
    char request[1024];
    snprintf(request, sizeof(request),
             "GET %s HTTP/1.1\r\n"
             "Host: %s\r\n"
             "Connection: close\r\n"
             "User-Agent: WeatherServer/1.0\r\n"
             "\r\n",
             parsed.path, parsed.host);

    if (send(sockfd, request, strlen(request), 0) < 0) {
        close(sockfd);
        free_parsed_url(&parsed);
        return -1;
    }

    // Read response
    char buffer[BUFFER_SIZE];
    ssize_t bytes_read;

    // Skip headers and find body
    int in_headers = 1;
    int is_chunked = 0;
    char* body_buffer = NULL;
    size_t body_size = 0;
    size_t body_capacity = 0;

    while ((bytes_read = recv(sockfd, buffer, sizeof(buffer) - 1, 0)) > 0) {
        buffer[bytes_read] = '\0';

        if (in_headers) {
            // Look for end of headers
            char* body_start = strstr(buffer, "\r\n\r\n");
            if (body_start) {
                // Check if Transfer-Encoding: chunked
                char* te_header = strcasestr(buffer, "Transfer-Encoding:");
                if (te_header) {
                    char* chunked = strcasestr(te_header, "chunked");
                    if (chunked && chunked < body_start) {
                        is_chunked = 1;
                    }
                }

                // Found end of headers
                body_start += 4;
                size_t body_len = bytes_read - (body_start - buffer);

                // Allocate body buffer
                body_capacity = 4096;
                body_buffer = malloc(body_capacity);
                if (!body_buffer) {
                    close(sockfd);
                    free_parsed_url(&parsed);
                    return -1;
                }

                // Copy initial body data
                memcpy(body_buffer, body_start, body_len);
                body_size = body_len;
                body_buffer[body_size] = '\0';

                in_headers = 0;
            }
            // If no body start found, all data is headers, continue
        } else {
            // Append body data
            if (body_size + bytes_read >= body_capacity) {
                size_t new_capacity = body_capacity * 2;
                while (new_capacity < body_size + bytes_read + 1) {
                    new_capacity *= 2;
                }
                char* new_buffer = realloc(body_buffer, new_capacity);
                if (!new_buffer) {
                    free(body_buffer);
                    close(sockfd);
                    free_parsed_url(&parsed);
                    return -1;
                }
                body_buffer = new_buffer;
                body_capacity = new_capacity;
            }
            memcpy(body_buffer + body_size, buffer, bytes_read);
            body_size += bytes_read;
            body_buffer[body_size] = '\0';
        }
    }

    // Process the body data
    if (is_chunked && body_buffer) {
        // Decode chunked encoding
        char* decoded = malloc(body_size + 1);
        if (!decoded) {
            free(body_buffer);
            close(sockfd);
            free_parsed_url(&parsed);
            return -1;
        }

        size_t decoded_size = 0;
        char* ptr = body_buffer;

        while (*ptr && ptr < body_buffer + body_size) {
            // Skip any leading whitespace
            while (*ptr == ' ' || *ptr == '\t' || *ptr == '\r' || *ptr == '\n') {
                ptr++;
            }
            if (!*ptr)
                break;

            // Read chunk size (hex number)
            char* endptr;
            long chunk_size = strtol(ptr, &endptr, 16);

            if (endptr == ptr || chunk_size < 0) {
                free(decoded);
                free(body_buffer);
                close(sockfd);
                free_parsed_url(&parsed);
                return -1;
            }

            // Skip to end of chunk size line
            ptr = strstr(endptr, "\r\n");
            if (!ptr) {
                free(decoded);
                free(body_buffer);
                close(sockfd);
                free_parsed_url(&parsed);
                return -1;
            }
            ptr += 2; // Skip \r\n

            if (chunk_size == 0) {
                break; // Last chunk
            }

            // Make sure we have enough data
            if (ptr + chunk_size > body_buffer + body_size) {
                free(decoded);
                free(body_buffer);
                close(sockfd);
                free_parsed_url(&parsed);
                return -1;
            }

            // Copy chunk data
            memcpy(decoded + decoded_size, ptr, chunk_size);
            decoded_size += chunk_size;
            ptr += chunk_size;

            // Skip trailing \r\n after chunk data
            if (ptr[0] == '\r' && ptr[1] == '\n') {
                ptr += 2;
            }
        }

        decoded[decoded_size] = '\0';
        free(body_buffer);
        body_buffer = decoded;
        body_size = decoded_size;
    }

    // Copy final result to response
    if (body_buffer) {
        if (append_to_response(response, body_buffer, body_size) != 0) {
            free(body_buffer);
            close(sockfd);
            free_parsed_url(&parsed);
            return -1;
        }
        free(body_buffer);
    }

    close(sockfd);
    free_parsed_url(&parsed);

    return bytes_read < 0 ? -1 : 0;
}
#ifndef CURL_CLIENT_H
#define CURL_CLIENT_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>

struct memory_struct {
    char* memory;
    size_t size;
};

typedef struct curl_client {
    CURLM* multi_handle;
    CURL* easy_handle;
    int still_running;
    struct memory_struct mem;
} curl_client;

int curl_client_init(curl_client** client);
int curl_client_make_request(curl_client** client, const char* url);
int curl_client_poll(curl_client** client);
int curl_client_read_response(curl_client** client, char** buffer);
int curl_client_cleanup(curl_client** client);

#endif
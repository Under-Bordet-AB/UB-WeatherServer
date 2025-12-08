#include "utilities/curl_client.h"
#include "global_defines.h"

int write_memory_callback(void* contents, size_t size, size_t nmemb, void* user_p) {
    size_t real_size = size * nmemb;

    struct memory_struct* mem = (struct memory_struct*)user_p;

    // Prevent exceeding centralized max response size
    size_t new_size = mem->size + real_size;
    if (new_size > CURL_CLIENT_MAX_RESPONSE_SIZE) {
        // Signal write failure to libcurl by returning 0
        return 0;
    }

    char* ptr = realloc(mem->memory, new_size + 1);
    if (ptr == NULL) { return 0; }

    mem->memory = ptr;
    memcpy(&(mem->memory[mem->size]), contents, real_size);
    mem->size = new_size;
    mem->memory[mem->size] = 0;

    return real_size;
}

int curl_client_init(curl_client** client) {
    CURLcode res = curl_global_init(CURL_GLOBAL_DEFAULT);
    if (res != CURLE_OK) { return -1; }

    (*client)->easy_handle = curl_easy_init();
    if (!(*client)->easy_handle) { return -1; }

    (*client)->multi_handle = curl_multi_init();
    if (!(*client)->multi_handle) {
        curl_easy_cleanup((*client)->easy_handle);
        return -1;
    }

    (*client)->mem.memory = malloc(1);
    if (!(*client)->mem.memory) {
        curl_multi_cleanup((*client)->multi_handle);
        curl_easy_cleanup((*client)->easy_handle);
        return -1;
    }
    (*client)->mem.size = 0;

    curl_easy_setopt((*client)->easy_handle, CURLOPT_WRITEFUNCTION, write_memory_callback);
    curl_easy_setopt((*client)->easy_handle, CURLOPT_WRITEDATA, (void*)&((*client)->mem));

    // Apply centralized timeout settings
    curl_easy_setopt((*client)->easy_handle, CURLOPT_CONNECTTIMEOUT, CURL_CONNECT_TIMEOUT_SEC);
    curl_easy_setopt((*client)->easy_handle, CURLOPT_TIMEOUT, CURL_REQUEST_TIMEOUT_SEC);

    return 0;
}

int curl_client_make_request(curl_client** client, const char* url) {
    curl_easy_setopt((*client)->easy_handle, CURLOPT_URL, url);
    curl_multi_add_handle((*client)->multi_handle, (*client)->easy_handle);

    return 0;
}

int curl_client_poll(curl_client** client) {
    CURLMcode mc = curl_multi_perform((*client)->multi_handle, &(*client)->still_running);
    if (mc != CURLM_OK) { return -1; }

    return 0;
}

int curl_client_read_response(curl_client** client, char** buffer) {
    if ((*client)->mem.size > 0) {
        *buffer = (char*)malloc((*client)->mem.size + 1);
        if (!*buffer) { return -1; }
        memcpy(*buffer, (*client)->mem.memory, (*client)->mem.size);
        (*buffer)[(*client)->mem.size] = '\0';
    } else {
        *buffer = NULL;
    }

    return 0;
}

int curl_client_cleanup(curl_client** client) {
    if ((*client)->mem.memory) {
        free((*client)->mem.memory);
        (*client)->mem.memory = NULL;
    }
    (*client)->mem.size = 0;

    if ((*client)->easy_handle) {
        curl_easy_cleanup((*client)->easy_handle);
        (*client)->easy_handle = NULL;
    }
    if ((*client)->multi_handle) {
        curl_multi_cleanup((*client)->multi_handle);
        (*client)->multi_handle = NULL;
    }
    curl_global_cleanup();

    return 0;
}

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Harness location: fuzz/harnesses/client_parse_fuzz.c
 * This file exercises the client's reading and parsing stages.
 * It simulates recv() data and tests the parsing of HTTP requests.
 */

#include "../../src/libs/http_parser.h"
#include "../../src/w_server/w_client.h"

// Simulate the client reading and parsing logic without actual sockets
int fuzz_client_parse(const char* data, size_t data_len) {
    // Simulate client read buffer (like w_client->read_buffer)
    char read_buffer[W_CLIENT_READ_BUFFER_SIZE];
    size_t bytes_read = 0;

    // Check if input fits in buffer
    if (data_len > sizeof(read_buffer) - 1) {
        // Simulate buffer full scenario
        return -1;
    }

    // Copy fuzz input into read buffer (simulating recv())
    memcpy(read_buffer, data, data_len);
    bytes_read = data_len;
    read_buffer[bytes_read] = '\0';

    // Check for complete HTTP request (client READING stage logic)
    // The client waits for "\r\n\r\n" to indicate complete request
    if (strstr(read_buffer, "\r\n\r\n") == NULL) {
        // Incomplete request - client would keep reading
        return 0;
    }

    // Now parse the request (client PARSING stage)
    http_request* parsed = http_request_fromstring(read_buffer);

    if (!parsed) {
        // Allocation failure
        return -2;
    }

    if (!parsed->valid) {
        // Parse error - would send 400 Bad Request
        http_request_dispose(&parsed);
        return -3;
    }

    // Successfully parsed - would transition to PROCESSING
    // Access parsed fields to ensure they're valid
    volatile request_method method = parsed->method;
    volatile const char* url = parsed->url;
    volatile protocol_version protocol = parsed->protocol;

    // Iterate headers to check linked list integrity
    if (parsed->headers != NULL) {
        LinkedList* headers = parsed->headers;
        for (size_t i = 0; i < headers->size; i++) {
            Node* node = LinkedList_get_index(headers, i);
            if (node && node->item) {
                HTTPHeader* hdr = (HTTPHeader*)node->item;
                volatile const char* name = hdr->name;
                volatile const char* value = hdr->value;
                (void)name;
                (void)value;
            }
        }
    }

    (void)method;
    (void)url;
    (void)protocol;

    http_request_dispose(&parsed);
    return 1;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("Usage: %s <input_file>\n", argv[0]);
        return 1;
    }

    const size_t BUF_SIZE = 65536;
    char* buf = malloc(BUF_SIZE);
    if (!buf)
        return 1;

    // Initialize buffer to zero for determinism
    memset(buf, 0, BUF_SIZE);

#ifdef __AFL_LOOP
    // Persistent mode for faster fuzzing
    while (__AFL_LOOP(1000)) {
#else
    do {
#endif
        FILE* f = fopen(argv[1], "rb");
        if (!f) {
            break;
        }

        // Read fuzz input from file
        size_t r = fread(buf, 1, BUF_SIZE - 1, f);
        fclose(f);

        if (r == 0) {
            continue;
        }

        // Null-terminate for safe string operations
        buf[r] = '\0';

        // Call the fuzzing target
        fuzz_client_parse(buf, r);

        // Reset buffer for next iteration
        memset(buf, 0, BUF_SIZE);
#ifdef __AFL_LOOP
    }
#else
    } while (0);
#endif

    free(buf);
    return 0;
}

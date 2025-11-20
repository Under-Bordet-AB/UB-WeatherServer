// ...existing code...
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Harness location: fuzz/harnesses/http_request_fuzz.c
 * This file exercises the HTTP parser: src/libs/http_parser.c
 * It expects input on stdin and calls http_request_fromstring().
 */

#include "../../src/libs/http_parser.h"

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
    // ifdef to support both single and persistent mode
    while (__AFL_LOOP(1000)) {
#else
    do {
#endif
        FILE* f = fopen(argv[1], "rb");
        if (!f) {
            break;
        }

        // read fuzz from file
        size_t r = fread(buf, 1, BUF_SIZE - 1, f);
        fclose(f);

        if (r == 0) {
            continue;
        }

        /*
        Null-terminate for safe string operations.
        if we dont it can lead to undefined behaviour that confuses the fuzzer
        Must test non-null terrminaton some other way.
        */
        buf[r] = '\0';

        // call function with fuzz
        http_request* req = http_request_fromstring(buf);
        if (req) {
            http_request_dispose(&req);
        }

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
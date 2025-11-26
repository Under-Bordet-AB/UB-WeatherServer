#include <stdio.h>
#include <string.h>

// Declare function implemented in geocache.c
char* geocache_normalize_name(const char* city_name, char* out_buf, size_t out_size);

int main(void) {
    const char* examples[] = {"Torsh%C3%A4lla", "Torsh%C3%85lla", "Torsh%C3%A5lla", "Torshälla", "TorshÄlla"};
    for (int i = 0; i < 5; i++) {
        char tmp[128];
        strncpy(tmp, examples[i], sizeof(tmp) - 1);
        tmp[sizeof(tmp) - 1] = '\0';

        // Emulate percent-decode step (done by utils_decode_swedish_chars in server)
        // We'll decode simple %HH sequences here
        char decoded[128];
        char *r = tmp, *w = decoded;
        while (*r) {
            if (*r == '%' && r[1] && r[2]) {
                char hex[3] = {r[1], r[2], 0};
                int v = (int)strtol(hex, NULL, 16);
                *w++ = (char)v;
                r += 3;
            } else {
                *w++ = *r++;
            }
        }
        *w = '\0';

        char out[128];
        geocache_normalize_name(decoded, out, sizeof(out));
        printf("orig=%s dec=%s norm=%s\n", examples[i], decoded, out);
    }
    return 0;
}

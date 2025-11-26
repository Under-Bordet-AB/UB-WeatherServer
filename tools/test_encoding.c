#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void decode_percent(char* str) {
    if (!str)
        return;
    char *r = str, *w = str;
    while (*r) {
        if (*r == '%' && isxdigit((unsigned char)r[1]) && isxdigit((unsigned char)r[2])) {
            char hex[3] = {r[1], r[2], 0};
            long v = strtol(hex, NULL, 16);
            *w++ = (char)v;
            r += 3;
        } else {
            *w++ = *r++;
        }
    }
    *w = '\0';
}

static void normalize_name(const char* in, char* out, size_t out_size) {
    if (!in || !out || out_size == 0)
        return;
    // skip leading ws
    while (*in && isspace((unsigned char)*in))
        in++;
    size_t j = 0;
    for (size_t i = 0; in[i] && j + 1 < out_size; i++) {
        unsigned char c = (unsigned char)in[i];
        if (c < 128) {
            out[j++] = (char)tolower(c);
        } else {
            out[j++] = in[i];
        }
    }
    // trim trailing ws
    while (j > 0 && isspace((unsigned char)out[j - 1]))
        j--;
    out[j] = '\0';
}

static void print_hex(const char* s) {
    for (size_t i = 0; s[i]; i++) {
        unsigned char c = (unsigned char)s[i];
        printf("%02X ", c);
    }
    printf("\n");
}

int main(void) {
    char examples[][64] = {"Torsh%C3%A4lla", "Torsh%C3%85lla", "Torsh%C3%A5lla", "Torshälla", "TorshÄlla"};

    for (int i = 0; i < (int)(sizeof(examples) / sizeof(examples[0])); i++) {
        char buf[128];
        strncpy(buf, examples[i], sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';
        printf("Original: %s\n", examples[i]);
        decode_percent(buf);
        printf("Decoded: %s\n", buf);
        printf("Bytes: ");
        print_hex(buf);
        char norm[128];
        normalize_name(buf, norm, sizeof(norm));
        printf("Normalized: %s\n", norm);
        printf("Norm bytes: ");
        print_hex(norm);
        printf("----\n");
    }
    return 0;
}

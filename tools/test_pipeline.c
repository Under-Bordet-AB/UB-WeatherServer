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

static void lowercase_swedish(unsigned char* s, size_t len) {
    for (size_t i = 0; i < len; i++) {
        unsigned char c = s[i];
        if (c < 128) {
            s[i] = (unsigned char)tolower(c);
        } else {
            unsigned char n = (i + 1 < len) ? s[i + 1] : 0;
            if (c == 0xC3 && n != 0) {
                if (n == 0x85) { /* Å -> å */
                    s[i] = 0xC3;
                    s[i + 1] = 0xA5;
                    i++;
                    continue;
                } else if (n == 0x84) { /* Ä -> ä */
                    s[i] = 0xC3;
                    s[i + 1] = 0xA4;
                    i++;
                    continue;
                } else if (n == 0x96) { /* Ö -> ö */
                    s[i] = 0xC3;
                    s[i + 1] = 0xB6;
                    i++;
                    continue;
                }
            }
        }
    }
}

static void percent_encode(const unsigned char* in, size_t in_len, char* out, size_t out_size) {
    size_t j = 0;
    for (size_t i = 0; i < in_len && j + 4 < out_size; i++) {
        unsigned char c = in[i];
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '.' ||
            c == '_' || c == '~') {
            out[j++] = (char)c;
        } else if (c == ' ') {
            out[j++] = '%';
            out[j++] = '2';
            out[j++] = '0';
        } else {
            int n = snprintf(&out[j], 4, "%%%02X", c);
            if (n != 3)
                break;
            j += 3;
        }
    }
    out[j] = '\0';
}

int main(void) {
    const char* examples[] = {"Torsh%C3%A4lla", "Torsh%C3%85lla", "Torsh%C3%A5lla", "Torshälla", "TorshÄlla"};
    for (int k = 0; k < 5; k++) {
        char buf[256];
        strncpy(buf, examples[k], sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';
        printf("Input: %s\n", buf);
        decode_percent(buf);
        printf("Decoded: %s\n", buf);
        size_t len = strlen(buf);
        lowercase_swedish((unsigned char*)buf, len);
        printf("Lowercased: %s\n", buf);
        char enc[512];
        percent_encode((unsigned char*)buf, strlen(buf), enc, sizeof(enc));
        printf("Re-encoded: %s\n", enc);
        printf("----\n");
    }
    return 0;
}

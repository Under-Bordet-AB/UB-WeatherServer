#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char** argv) {
    const char* ip = "127.0.0.1";
    int port = 10480;
    int total = 10;           // default: 10 clients
    int interval_us = 100000; // default: 100 ms
    const char* msg = "HELLO";

    if (argc >= 2)
        ip = argv[1];
    if (argc >= 3)
        port = atoi(argv[2]);
    if (argc >= 4)
        total = atoi(argv[3]);
    if (argc >= 5)
        interval_us = atoi(argv[4]);
    if (argc >= 6)
        msg = argv[5];

    printf("Single-client load test: %d connections → %s:%d, interval %dus\n", total, ip, port, interval_us);

    for (int i = 0; i < total; i++) {
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) {
            perror("socket");
            continue;
        }

        struct sockaddr_in addr = {0};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        inet_pton(AF_INET, ip, &addr.sin_addr);

        if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            perror("connect");
            close(sock);
            continue;
        }

        if (msg && strlen(msg) > 0) {
            send(sock, msg, strlen(msg), 0);
        }

        char buf[1024];
        ssize_t n = recv(sock, buf, sizeof(buf) - 1, 0);
        if (n > 0) {
            buf[n] = '\0';
            printf("[%d] Received: %s\n", i + 1, buf);
        }

        close(sock);
        usleep(interval_us);
    }

    printf("All clients finished\n");
    return 0;
}

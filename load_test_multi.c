#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/time.h>
#include <unistd.h>

#define DEFAULT_IP "127.0.0.1"
#define DEFAULT_PORT 10480
#define DEFAULT_CONN 10
#define DEFAULT_MSG "HELLO\n"

typedef struct {
    int fd;
    int connected;
} client_t;

int main(int argc, char** argv) {
    const char* ip = DEFAULT_IP;
    int port = DEFAULT_PORT;
    int total = DEFAULT_CONN;
    const char* msg = DEFAULT_MSG;

    if (argc >= 2)
        ip = argv[1];
    if (argc >= 3)
        port = atoi(argv[2]);
    if (argc >= 4)
        total = atoi(argv[3]);
    if (argc >= 5)
        msg = argv[4];

    client_t* clients = calloc(total, sizeof(client_t));
    if (!clients) {
        perror("calloc");
        return 1;
    }

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, ip, &addr.sin_addr);

    printf("Starting %d connections to %s:%d...\n", total, ip, port);

    // Open all sockets in non-blocking mode
    for (int i = 0; i < total; i++) {
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) {
            perror("socket");
            continue;
        }

        int flags = fcntl(fd, F_GETFL, 0);
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);

        int r = connect(fd, (struct sockaddr*)&addr, sizeof(addr));
        if (r < 0 && errno != EINPROGRESS) {
            perror("connect");
            close(fd);
            continue;
        }

        clients[i].fd = fd;
        clients[i].connected = 0;
    }

    // Wait for connections to complete using select()
    int remaining = total;
    while (remaining > 0) {
        fd_set wfds;
        FD_ZERO(&wfds);
        int maxfd = 0;

        for (int i = 0; i < total; i++) {
            if (clients[i].fd > 0 && !clients[i].connected) {
                FD_SET(clients[i].fd, &wfds);
                if (clients[i].fd > maxfd)
                    maxfd = clients[i].fd;
            }
        }

        struct timeval tv = {0, 1000}; // 1ms
        int n = select(maxfd + 1, NULL, &wfds, NULL, &tv);
        if (n < 0) {
            perror("select");
            break;
        }

        for (int i = 0; i < total; i++) {
            if (clients[i].fd > 0 && !clients[i].connected && FD_ISSET(clients[i].fd, &wfds)) {
                clients[i].connected = 1;

                // Send message
                ssize_t s = send(clients[i].fd, msg, strlen(msg), 0);
                if (s < 0)
                    perror("send");
                remaining--;
            }
        }
    }

    printf("All clients connected and sent message.\n");

    // Wait a bit before closing
    sleep(2);
    for (int i = 0; i < total; i++) {
        if (clients[i].fd > 0)
            close(clients[i].fd);
    }
    free(clients);
    return 0;
}

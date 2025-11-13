/* ----------------------------------------------------------------------------------------
   w_server.c – Init the server, set up listening socket. Does not start listening.

   The server does no work. Its just holds  global data used by connections
   ------------------------------------------------------------------------------------- */

#include "w_server.h"
#include "w_client.h"
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

//////////////////////////////////////////////////////////////////////////
// FRÅN AI INNE I DENNA
/*
// State: Always in "ACCEPTING" state
void w_server_accept_clients_func(mj_scheduler* scheduler, void* state) {
    int* listen_fd = (int*)state;

    // Try to accept (non-blocking)
    struct sockaddr_storage client_addr;
    socklen_t addr_len = sizeof(client_addr);
    int client_fd = accept(*listen_fd, (struct sockaddr*)&client_addr, &addr_len);

    if (client_fd < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            // No clients waiting - that's OK, just return
            return;
        }
        // Real error - handle it
        return;
    }

    // Got a client! Make it non-blocking too
    fcntl(client_fd, F_SETFL, O_NONBLOCK);

    // Create client state machine
    w_client* client = malloc(sizeof(w_client));
    client->fd = client_fd;
    client->state = CLIENT_STATE_READ_REQUEST;
    // ... initialize other fields

    // Add client's state machine to scheduler
    mj_scheduler_task_add(scheduler, w_client_task_func, client);
}
*/
//////////////////////////////////////////////////////////////////////////

// Listen for new clients. This is the function that gets added to the scheduler for running
void w_server_listen_TCP_nonblocking(mj_scheduler* scheduler, void* user_data) {
    printf("Listening...\n");
}

void w_server_listen_TCP_nonblocking_destroy(mj_scheduler* scheduler, void* user_data) {
    printf("Stopping listening and unbinding listening socket...\n");
}

static int init_from_config(w_server* server, const w_server_config* cfg) {
    if (!server || !cfg)
        return W_SERVER_ERROR_INVALID_PORT;

    /* address string – keep as‑is (may be empty for INADDR_ANY) */
    if (cfg->address && cfg->address[0] != '\0') {
        strncpy(server->address, cfg->address, sizeof(server->address) - 1);
        server->address[sizeof(server->address) - 1] = '\0';
    } else {
        server->address[0] = '\0'; /* empty → bind() will use INADDR_ANY */
    }

    /* port string – required for getaddrinfo() */
    if (cfg->port && cfg->port[0] != '\0') {
        strncpy(server->port, cfg->port, sizeof(server->port) - 1);
        server->port[sizeof(server->port) - 1] = '\0';
    } else {
        return W_SERVER_ERROR_INVALID_PORT;
    }

    /* initialise counters */
    server->active_count = 0;
    server->last_error = W_SERVER_ERROR_NONE;

    return W_SERVER_ERROR_NONE;
}

// Open a listening socket
int w_server_create(w_server* server, w_server_config* config) {
    if (!server || !config) {
        return W_SERVER_ERROR_INVALID_PORT; /* generic error for bad args */
    }

    int rc = init_from_config(server, config);
    if (rc != W_SERVER_ERROR_NONE) {
        server->last_error = rc;
        return rc;
    }

    /* Resolve the address/port with getaddrinfo() */
    struct addrinfo hints, *res, *p;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC; /* IPv4 or IPv6 */
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE; /* for bind() */

    int gai_err = getaddrinfo(server->address[0] ? server->address : NULL, server->port, &hints, &res);
    if (gai_err != 0) {
        server->last_error = W_SERVER_ERROR_GETADDRINFO;
        return W_SERVER_ERROR_GETADDRINFO;
    }

    /* Try each returned address until we succeed */
    for (p = res; p != NULL; p = p->ai_next) {
        server->listen_fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (server->listen_fd == -1)
            continue;

        /* Quick port reuse after program exit */
        int opt = 1;
        setsockopt(server->listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        if (bind(server->listen_fd, p->ai_addr, p->ai_addrlen) == -1) {
            close(server->listen_fd);
            server->listen_fd = -1;
            continue; /* try next */
        }

        break; /* success */
    }
    freeaddrinfo(res);

    if (server->listen_fd == -1) {
        server->last_error = W_SERVER_ERROR_SOCKET_BIND;
        return W_SERVER_ERROR_SOCKET_BIND;
    }
    mj_task* task = calloc(1, sizeof(*task));
    if (!task) {
        server->last_error = W_SERVER_ERROR_MEMORY_ALLOCATION;
        return W_SERVER_ERROR_MEMORY_ALLOCATION;
    }

    // Set functions for scheduler
    task->create = NULL;
    task->run = w_server_listen_TCP_nonblocking;
    task->destroy = w_server_listen_TCP_nonblocking_destroy;
    task->user_data = NULL;

    server->w_server_listen_tasks = task;

    /* Success – the socket is bound but not listening yet. */
    server->last_error = W_SERVER_ERROR_NONE;
    return 0; /* conventionally 0 = success */
}

/*  Destroy – close the socket
--------------------------------------------------------------- */
void w_server_destroy(w_server* server) {
    if (!server)
        return;

    if (server->listen_fd >= 0) {
        close(server->listen_fd);
        server->listen_fd = -1;
    }
}

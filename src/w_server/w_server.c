/* ----------------------------------------------------------------------------------------
   w_server.c
        - Init the server
        - Sets up listening socket and marks it as listening
        - add client accept funciton to the scheduler ( w_server_listen_TCP_nonblocking() )

   The server does no work. Its just starts the listening task and holds global data
   ------------------------------------------------------------------------------------- */
#define _GNU_SOURCE /* expose accept4() and other GNU extensions */
#include "w_server.h"
#include "../utils/ui.h"
#include "global_defines.h"
#include "w_client.h"
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

// TODO w_server_listen_TCP_nonblocking() gets added as a task (along with a cleanup function) to the scheduler.
// Therfore it should be a separate module that plugs into the server. Since we can listen on sockets in many ways.
// Each of these ways gets its own funciton.

// Listen for new clients non-blocking. This is the function that gets added to the scheduler
void w_server_listen_TCP_nonblocking(mj_scheduler* scheduler, void* ctx) {
    w_server* server = (w_server*)ctx;
    int listen_fd = server->listen_fd;
    int client_fd = -1;
    int accepts_this_tick = 0;

    // Try to accept (non-blocking)
    struct sockaddr_storage client_addr;
    socklen_t addr_len = sizeof(client_addr);

    // Accept more than one client per tick
    while (accepts_this_tick < MAX_ACCEPTS_PER_TICK) {
        // accept4 gets 4 clients at a time. Also set FDs to non blocking
        client_fd = accept4(listen_fd, (struct sockaddr*)&client_addr, &addr_len, SOCK_NONBLOCK);
        if (client_fd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // No clients waiting, just return
                return;
            }
            ui_print_server_listen_error("W_SERVER_ERROR_SOCKET_LISTEN");
            return;
        }

        // Create client task
        mj_task* new_task = w_client_create(client_fd, server);
        if (!new_task) {
            ui_print_server_client_accept_error("task creation failed");
            close(client_fd);
            continue;
        }

        // Add client's state machine to scheduler
        mj_scheduler_task_add(scheduler, new_task);

        accepts_this_tick++;
    }
}

// TODO finish clean up function
void w_server_listen_TCP_nonblocking_cleanup(mj_scheduler* scheduler, void* ctx) {
    w_server* server = (w_server*)ctx;

    ui_print_server_listen_stopped(server->listen_fd);
}

static int init_from_config(w_server* server, const w_server_config* cfg) {
    if (!server || !cfg)
        return W_SERVER_ERROR_INVALID_CONFIG;

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
    server->total_clients = 0;
    server->last_error = W_SERVER_ERROR_NONE;

    return W_SERVER_ERROR_NONE;
}

// Creates the server and opens its listening socket
w_server* w_server_create(w_server_config* config) {
    /* TODO server init function, error handling:
    - Should we only return a server on no errors? Then we can use "goto cleanup" pattern for errors.
    - Since server can fail we should check errors back in main.
        int error= 0
        pointer*  server =  init_server(&cfg, &error)
        if (error !=0 || server == NULL)
            print(error)
    - standardize error handling from getaddrinfo to align with rest of setup
*/
    if (!config) {
        ui_print_server_init_error("W_SERVER_ERROR_NO_CONFIG");
        return NULL;
    }
    w_server* server = calloc(1, sizeof(*server));
    if (server == NULL) {
        ui_print_server_init_error("W_SERVER_ERROR_MEMORY_ALLOCATION");
        return NULL;
    }

    int result = init_from_config(server, config);
    if (result != W_SERVER_ERROR_NONE) {
        ui_print_server_init_error("W_SERVER_ERROR_INVALID_CONFIG");
        free(server);
        return NULL;
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
        ui_print_server_init_error("W_SERVER_ERROR_GETADDRINFO");
        free(server);
        return NULL;
    }

    /* Try each returned address until we succeed */
    for (p = res; p != NULL; p = p->ai_next) {
        server->listen_fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (server->listen_fd == -1) {
            continue;
        }

        // Set SO_REUSEADDR before bind() to allow quick port reuse
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

    // Did we mange to bind to an address?
    if (server->listen_fd == -1) {
        server->last_error = W_SERVER_ERROR_SOCKET_BIND;
        ui_print_server_init_error("W_SERVER_ERROR_SOCKET_BIND");
        free(server);
        return NULL;
    }

    //// Set listening socket options
    // Set non-blocking
    int flags = fcntl(server->listen_fd, F_GETFL, 0);
    fcntl(server->listen_fd, F_SETFL, flags | O_NONBLOCK);
    // Mark socket as listening in the OS
    if (listen(server->listen_fd, 4096) < 0) { // SOMAXCONN
        perror("listen");
        free(server);
        return NULL;
    }

    // Create a new task for the listening task
    mj_task* task = calloc(1, sizeof(*task));
    if (task == NULL) {
        server->last_error = W_SERVER_ERROR_MEMORY_ALLOCATION;
        ui_print_server_init_error("W_SERVER_ERROR_MEMORY_ALLOCATION");
        free(server);
        return NULL;
    }

    // Configure task
    task->create = NULL;
    task->run = w_server_listen_TCP_nonblocking;
    task->cleanup = w_server_listen_TCP_nonblocking_cleanup;
    task->ctx = server; // The listening task needs access to the whole server struct. FD, active clients etc,

    // Set servers listening task
    server->w_server_listen_task = task;

    // Start to listen

    // All done, return the surver address
    if (server->last_error == W_SERVER_ERROR_NONE) {
        return server;
    }
    // Unknown error
    ui_print_server_init_error("W_SERVER_ERROR");
    free(task);
    free(server);
    return NULL;
}

/*  Destroy – close the socket
--------------------------------------------------------------- */
void w_server_cleanup(w_server* server) {
    if (!server)
        return;

    if (server->listen_fd >= 0) {
        close(server->listen_fd);
        server->listen_fd = -1;
    }
}

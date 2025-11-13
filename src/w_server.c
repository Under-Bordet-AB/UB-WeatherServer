/* ----------------------------------------------------------------------------------------
   w_server.c – Init the server, set up listening socket. Does not start listening.

   The server does no work. Its just holds  global data used by connections
   ------------------------------------------------------------------------------------- */

#include "w_server.h"
#include "w_client.h"
#include <netdb.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

/* ----------------------------------------------------------------------
 *      PRIVATE FUNCTIONS
 * ---------------------------------------------------------------------- */

/*  Task function that gets added to the scheduler.
        It listens on the servers listening socket and creates new clients
        and adds these clients to the scheduler.
--------------------------------------------------------------- */
void w_server_accept_clients_func(mj_scheduler* scheduler, void* state) {
    int* listen_fd = (int*)state;

    // Lyssna på socketen

    // Malloca en client
    w_client* w_client;

    // lägg till klientens state machine till schedulern,
    mj_scheduler_task_add(scheduler, CLIENT_TASK_FN, w_client);
}

//////////////////////////////////////////////////////////////////////////
// FRÅN AI INNE I DENNA

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
//////////////////////////////////////////////////////////////////////////

/*  Initialise fields from config
--------------------------------------------------------------- */
static int init_from_config(w_server* srv, const w_server_config* cfg) {
    if (!srv || !cfg)
        return W_SERVER_ERROR_INVALID_PORT;

    /* address string – keep as‑is (may be empty for INADDR_ANY) */
    if (cfg->address && cfg->address[0] != '\0') {
        strncpy(srv->address, cfg->address, sizeof(srv->address) - 1);
        srv->address[sizeof(srv->address) - 1] = '\0';
    } else {
        srv->address[0] = '\0'; /* empty → bind() will use INADDR_ANY */
    }

    /* port string – required for getaddrinfo() */
    if (cfg->port && cfg->port[0] != '\0') {
        strncpy(srv->port, cfg->port, sizeof(srv->port) - 1);
        srv->port[sizeof(srv->port) - 1] = '\0';
    } else {
        return W_SERVER_ERROR_INVALID_PORT;
    }

    /* initialise counters */
    srv->active_count = 0;
    srv->last_error = W_SERVER_ERROR_NONE;

    return W_SERVER_ERROR_NONE;
}

/* ----------------------------------------------------------------------
 *      PUBLIC API
 * ---------------------------------------------------------------------- */

/*  Create – open socket and bind it
 --------------------------------------------------------------- */
int w_server_create(mj_scheduler* scheduler, w_server* server, const w_server_config* config) {
    if (!scheduler || !server || !config) {
        return W_SERVER_ERROR_INVALID_PORT; /* generic error for bad args */
    }

    server->scheduler = scheduler;
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

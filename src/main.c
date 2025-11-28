#include "majjen.h"
#include "w_server.h"
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Global shutdown flag (accessed by signal handler and scheduler)
volatile sig_atomic_t shutdown_requested = 0;

// Signal handler for SIGINT (Ctrl+C)
static void sigint_handler(int signum) {
    (void)signum; // Unused parameter
    shutdown_requested = 1;
}

int main(int argc, char* argv[]) {
    ////////////////////////////////////////////
    //////// SETUP

    // Set defaults
    const char* port = "10480";        // Default port
    const char* address = "127.0.0.1"; // Default bind address (localhost)

    // Parse command line arguments "./server 8080 192.168.1.100"
    if (argc > 1) {
        port = argv[1];
    }
    if (argc > 2) {
        address = argv[2];
    }

    printf("\033[2J\033[H"); // Clear terminal and move cursor to top-left
    printf("=== UB Weather Server ===\n\n");
    printf("Configuration:\n");
    printf("  Bind address : %s\n", address);
    printf("  Port         : %s\n", port);

    if (strcmp(address, "127.0.0.1") == 0 || strcmp(address, "localhost") == 0) {
        printf("  Note         : Listening on localhost only. Only clients on this machine can connect.\n");
        printf("                 To allow external connections, use 0.0.0.0 or the server's network IP.\n");
    }

    printf("\nAvailable endpoints:\n");
    printf("  /weather?location=<x>  - Weather lookup for <x>\n");
    printf("  /index.html            - Server monitoring webpage\n");
    printf("  /surprise              - Surprise endpoint\n");
    printf("  /health                - Returns \"OK\" if the server is alive\n");
    printf("  /                      - Hello message\n");

    fflush(stdout);

    // Create server configuration
    w_server_config config = {.address = address, .port = port, .listening_backlog = SOMAXCONN};

    // Create the cooperative scheduler
    mj_scheduler* scheduler = mj_scheduler_create();
    if (!scheduler) {
        fprintf(stderr, "Failed to create scheduler\n");
        return 1;
    }

    // Set up signal handler
    struct sigaction sa;
    sa.sa_handler = sigint_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    // Create the weather server
    w_server* server = w_server_create(&config);
    if (server == NULL) {
        fprintf(stderr, "Failed to create weather server\n");
        mj_scheduler_destroy(&scheduler);
        return 1;
    }

    // Add the server's listening task to the scheduler
    if (mj_scheduler_task_add(scheduler, server->w_server_listen_task) < 0) {
        fprintf(stderr, "Failed to add server listen task to scheduler\n");
        mj_scheduler_destroy(&scheduler);
        return 1;
    }

    ////////////////////////////////////////////
    //////// PROGRAM STARTS HERE

    printf("\nServer starting...\n");
    printf("Listening on %s:%s\n", address, port);
    printf("Use a client like `curl http://%s:%s` to connect\n", address, port);
    printf("Press Ctrl+C to stop the server\n\n");

    // Start the cooperative scheduler (blocks here until shutdown)
    int result = mj_scheduler_run(scheduler);
    if (result != 0) {
        fprintf(stderr, "Scheduler exited with error: %d\n", result);
    }

    ////////////////////////////////////////////
    //////// PROGRAM ENDS HERE

    // Cleanup
    if (shutdown_requested) {
        printf("\nShutdown signal received. Cleaning up...\n");
    } else {
        printf("Shutting down server...\n");
    }

    w_server_destroy(&server);

    mj_scheduler_destroy(&scheduler);

    printf("Server stopped cleanly.\n");
    return 0;
}
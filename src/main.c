#include "majjen.h"
#include "w_server.h"
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

/*
// Increase fd limit for this process
#include <sys/resource.h>
struct rlimit rl = { .rlim_cur = 1000000, .rlim_max = 2000000 };
setrlimit(RLIMIT_NOFILE, &rl);
 */

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
    printf("Configuring server...\n");
    printf("Bind address: %s\n", address);
    printf("Port: %s\n", port);
    if (address[0] == '1' && address[1] == '2' && address[2] == '7') {
        printf("Note: Server is bound to localhost. Only clients on this machine can connect.\n");
        printf("To allow external connections, use 0.0.0.0 or the server's network IP.\n");
    }

    // Create server configuration
    w_server_config config = {.address = address, .port = port, .backlog = SOMAXCONN};

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

    // Clean up all remaining tasks in the scheduler
    // Note: The listening task's ctx points to the server struct,
    // so it will be freed during this call
    mj_scheduler_cleanup_all_tasks(scheduler);

    mj_scheduler_destroy(&scheduler);

    printf("Server stopped cleanly.\n");
    return 0;
}

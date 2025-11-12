#include "majjen.h"
#include "w_server.h"
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char* argv[]) {
    ////////////////////////////////////////////
    //////// SETUP

    // Set defaults
    const char* port = "10480";        // Our port on the server
    const char* address = "127.0.0.1"; // Bind to localhost by default

    // Parse command line arguments "./server 8080 192.168.1.100"
    if (argc > 1) {
        port = argv[1];
    }
    if (argc > 2) {
        address = argv[2];
    }

    // Create server configuration
    w_server_config config = {
        .address = address,
        .port = port,
        .backlog = SOMAXCONN // SOMAXCONN can be changed. Check with "cat /proc/sys/net/core/somaxconn"
                             // "ss -tlpn '( sport = :80 )'" shows backlog
    };

    printf("Starting server...\n");
    printf("Port: %s\n", port);
    printf("Address: %s\n", address);

    // Create the cooperative scheduler
    mj_scheduler* scheduler = mj_scheduler_create();
    if (!scheduler) {
        fprintf(stderr, "Failed to create scheduler\n");
        return 1;
    }

    // Create and initialize the weather server
    w_server server; // TODO would be nice to make type opaque
    if (w_server_create(scheduler, &server, &config) != 0) {
        fprintf(stderr, "Failed to create weather server\n");
        mj_scheduler_destroy(&scheduler);
        return 1;
    }

    ////////////////////////////////////////////
    //////// PROGRAM STARTS HERE

    // Add listening task to scheduler

    // Run the cooperative scheduler (blocks here until shutdown)
    int result = mj_scheduler_run(scheduler);
    if (result != 0) {
        fprintf(stderr, "Scheduler exited with error: %d\n", result);
    }

    ////////////////////////////////////////////
    //////// PROGRAM ENDS HERE

    // Cleanup
    printf("Shutting down server...\n");
    mj_scheduler_destroy(&scheduler);
    w_server_destroy(&server);

    printf("Server stopped cleanly.\n");
    return 0;
}
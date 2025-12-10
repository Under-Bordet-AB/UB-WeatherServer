#include <signal.h>
#include <stdio.h>

#include "global_defines.h"
#include "smw.h"
#include "utils.h"

#include "WeatherServer.h"

// Ovanför main
static volatile int g_running = 1;
static void signal_handler(int signum) {
    (void)signum;
    g_running = 0;
}

int main() {

    smw_init();

    WeatherServer* server = NULL;
    WeatherServer_InitiatePtr(&server);

    if (server == NULL) {
        printf("Error: Failed to initialize WeatherServer\n");
        return 1;
    }

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    while (g_running) {
        uint64_t now = SystemMonotonicMS();
        smw_work(now);
    }

    WeatherServer_DisposePtr(&server);

    smw_dispose();

    return 0;
}
#include <stdio.h>
#include <signal.h>

#include "smw.h"
#include "utils.h"
#include "global_defines.h"

#include "WeatherServer.h"

// Ovanför main
static volatile int g_running = 1;
static void signal_handler(int signum)
{
    (void)signum;
    g_running = 0;
}

int main() {
    
    smw_init();

    WeatherServer server;
    WeatherServer_Initiate(&server);

    /* Print startup info from central defines so we know where server is bound */
    printf("Info: server started on port %s\n", WeatherServer_LISTEN_PORT);

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    while (g_running) {
        uint64_t now = SystemMonotonicMS(); 
        smw_work(now);
    }

    WeatherServer_Dispose(&server);

    smw_dispose();

    return 0;
}
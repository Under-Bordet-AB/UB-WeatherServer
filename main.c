#include <stdio.h>
#include <signal.h>

#include "smw.h"
#include "utils.h"

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

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    while (g_running) {
        uint64_t now = SystemMonotonicMS(); // debuggern kliver aldrig in i smw_work i vscode om vi inte gör så här
        smw_work(now);
        // usleep(10000);
    }

    WeatherServer_Dispose(&server);

    smw_dispose();

    return 0;
}

// TODO: Write function: "HTTPServerConnection_SendResponse()".
// TODO: Write WeatherServerInstance state-machine with mock-responses.
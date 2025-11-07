/*
THIS IS HOW THE DATA LOOKS LIKE:
{
    LinkedList* instances;
    smw_task* task;
    HTTPServer {
        HTTPServer_OnConnection onConnection;
        smw_task* task;
        TCPServer {
            int listen_fd;
            TCPServer_OnAccept onAccept;
            void* context;
            smw_task* task;
        };
    };
};
*/

#include "WeatherServer.h"
#include "smw.h"
#include "utils.h"
#include <stdio.h>

int main() {
    smw_init();

    WeatherServer server;
    WeatherServer_Initiate(&server);

    /* Run the scheduler frequently so connection tasks don't time out.
       Earlier the loop slept 1s which caused the HTTP connection code to
       time out (HTTPSERVER_TIMEOUT_MS = 1000 ms). Call smw_work in a
       short loop with a small sleep. */
    while (1) {
        smw_work(SystemMonotonicMS());
        /* sleep 10ms to avoid busy loop */

        usleep(10000);
    }

    WeatherServer_Dispose(&server);

    smw_dispose();

    return 0;
}

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

    while (1) {
        smw_work(SystemMonotonicMS());
        usleep(10000);
    }

    WeatherServer_Dispose(&server);

    smw_dispose();

    return 0;
}

// TODO: Write function: "HTTPServerConnection_SendResponse()".
// TODO: Write WeatherServerInstance state-machine with mock-responses.
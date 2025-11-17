#include <stdio.h>

#include "smw.h"
#include "utils.h"

#include "WeatherServer.h"

int main() {
    smw_init();

    WeatherServer server;
    WeatherServer_Initiate(&server);

    while (1) {
        uint64_t now = SystemMonotonicMS(); // debuggern kliver aldrig in i smw_work i vscode om vi inte gör så här
        smw_work(now);
        // usleep(50000);
    }

    WeatherServer_Dispose(&server);

    smw_dispose();

    return 0;
}

// TODO: Write function: "HTTPServerConnection_SendResponse()".
// TODO: Write WeatherServerInstance state-machine with mock-responses.
#include <stdio.h>
#include <signal.h>

#include "smw.h"
#include "utils.h"

#include "WeatherServer.h"

#include "utilities/http_client.h"
#include "HTTPParser.h"

#define METEO_FORECAST_URL                                                                                                                                     \
    "https://api.open-meteo.com/v1/"                                                                                                                           \
    "forecast?latitude=59.00&longitude=25.00&current=temperature_2m,relative_humidity_2m,apparent_temperature,is_day,precipitation,rain,showers,snowfall,weather_"   \
    "code,cloud_cover,pressure_msl,surface_pressure,wind_speed_10m,wind_direction_10m,wind_gusts_10m"

// Ovanför main
static volatile int g_running = 1;
static void signal_handler(int signum)
{
    (void)signum;
    g_running = 0;
}

void hello(HTTPClient* _client, const char* _event) {

    printf("Buffer: %s\n", _client->buffer);

}

int main() {
    smw_init();

    WeatherServer server;
    WeatherServer_Initiate(&server);

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // HTTPClient* client = malloc(sizeof(HTTPClient));
    // HTTPClient_Initiate(client);

    // HTTPClient_GET(client, METEO_FORECAST_URL, hello);

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
#include <ctype.h>
#include <stdio.h>
#include <signal.h>
#include <string.h>
#include "smw.h"
#include "utils.h"
#include "global_defines.h"
#include "WeatherServer.h"

static volatile int g_running = 1;
static void signal_handler(int signum)
{
    (void)signum;
    g_running = 0;
}

int main(int argc, char *argv[]) {

	if (argc != 2)
	{
		printf("Usage: %s <port>\n", argv[0]);
		return -1;
	}
	for (size_t i = 0; argv[1][i] != '\0'; i++)
	{
		if (!isdigit((unsigned char)argv[1][i]))
		{
			printf("Expected integer but got %s.\n", argv[1]);
			return -1;
		}
	}
	if (strlen(argv[1]) > (LISTEN_PORT_MAX_SIZE - 1))
	{
		printf("Given port does not fit in max value!\n");
		return -1;
	}
	char port[LISTEN_PORT_MAX_SIZE] = {0};
	strncpy(port, argv[1], LISTEN_PORT_MAX_SIZE - 1);
	port[LISTEN_PORT_MAX_SIZE -1] = '\0';
	int port_range = atoi(port);
	if (port_range < 1 || port_range > LISTEN_PORT_RANGE)
	{
		printf("Port: %d, is not within range 1 - %d\n", port_range, LISTEN_PORT_RANGE);
		return -1;
	}
    
    smw_init();
    WeatherServer server;
    WeatherServer_Initiate(&server, port);
    printf("Info: server started on port %s\n", port);
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

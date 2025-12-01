#pragma once
/*
    TODO This should be a config in the server. But for now we just gather everything in one place atleast.
*/

//// UI
// UI printing control flag
#define UI_PRINT_ENABLED 1

//// SERVER
// How many times to call accept4() per tick / entry into the listening function
#define MAX_ACCEPTS_PER_TICK 16 // 16 * 4 = 64 new clients can be accepted per tick
#define MAX_SEND_RETRIES 10     // Lets client retry immediately if it was interrupted (errno == EINTR)

//// CONNECTED CLIENTS
#define W_CLIENT_READ_BUFFER_SIZE 8192
// TODO fix these two so that one defines the other
#define W_CLIENT_REQ_LOCATION_MAX_SIZE 128       // longest location to try to find
#define W_CLIENT_REQ_LOCATION_MAX_SIZE_STR "128" // Macro to convert number to string for sscanf
#define MAX_URL_LEN 256

// For how long do we let a connected client send its request
#define CLIENT_READING_TIMEOUT_SEC 10
// TODO [ NOT IMPLEMENTED ] How many connections can be active per IP
#define CLIENT_MAX_CONNECTIONS_PER_IP
// TODO [ NOT IMPLEMENTED ] How many connections per second can a IP perform
#define CLIENT_MAX_CONNECTIONS_PER_IP_PER_SECOND

//// BACKEND
#define BACKEND_METEO_CALL_TIMEOUT_SECONDS 60
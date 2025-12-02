#ifndef GEOLOCATION_H
#define GEOLOCATION_H

#include <jansson.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "utilities/curl_client.h"

#define METEO_GEOLOCATION_URL "https://geocoding-api.open-meteo.com/v1/search?name=%s&count=%d&language=en&format=json"

typedef enum {
    GeoLocation_State_Init,
    // GeoLocation_State_LoadFromDisk,
    GeoLocation_State_FetchFromAPI_Init,
    GeoLocation_State_FetchFromAPI_Request,
    GeoLocation_State_FetchFromAPI_Poll,
    GeoLocation_State_FetchFromAPI_Read,
    GeoLocation_State_ProcessResponse,
    // GeoLocation_State_SaveToDisk,
    GeoLocation_State_Done
} geolocation_state;

typedef struct location_t {
    int id;
    char* name;
    int longitude;
    int latitude;
    double elevation;
    char* feature_code;
    char* country_code;

    int admin1_id;
    int admin2_id;
    int admin3_id;
    int admin4_id;

    char* timezone;
    int population;
    char** postcodes;
    size_t postcodes_count;
    int country_id;
    char* country;

    char* admin1;
    char* admin2;
    char* admin3;
    char* admin4;
} location_t;

typedef struct geolocation_t {
    void* ctx;
    void (*on_done)(void* ctx);

    curl_client_t* curl_client;

    char* location_name;
    int location_count;
    char* country_code;

    geolocation_state state;
    char* buffer;
    int bytesread;
} geolocation_t;

// Server functions
int geolocation_set_parameters(void** ctx, char* location_name, int location_count, char* country_code);

int geolocation_init(void** ctx, void** ctx_struct, void (*on_done)(void* context));
int geolocation_work(void** ctx);
int geolocation_get_buffer(void** ctx, char** buffer);
int geolocation_dispose(void** ctx);

// Internal parsing functions
int parse_openmeteo_geo_json_to_location(const json_t* json_obj, location_t* location);
int serialize_location_to_json(const location_t* location, json_t** json_obj);
void free_location(location_t* location);

// Client functions
int deserialize_geolocation_response(const char* client_response, location_t** location);

#endif
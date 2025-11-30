#ifndef WEATHER_H
#define WEATHER_H

#include <jansson.h>
#include <stdio.h>
#include <stdlib.h>

#include "utilities/curl_client.h"

#define METEO_FORECAST_URL                                                                                                                                     \
    "https://api.open-meteo.com/v1/"                                                                                                                           \
    "forecast?latitude=%f&longitude=%f&current=temperature_2m,relative_humidity_2m,apparent_temperature,is_day,precipitation,rain,showers,snowfall,weather_"   \
    "code,cloud_cover,pressure_msl,surface_pressure,wind_speed_10m,wind_direction_10m,wind_gusts_10m"

typedef enum {
    Weather_State_Init,
    Weather_State_ValidateFile,
    Weather_State_LoadFromDisk,
    Weather_State_FetchFromAPI_Init,
    Weather_State_FetchFromAPI_Request,
    Weather_State_FetchFromAPI_Poll,
    Weather_State_FetchFromAPI_Read,
    Weather_State_ProcessResponse,
    Weather_State_SaveToDisk,
    Weather_State_Done
} weather_state;

typedef struct weather_t {
    void* ctx;
    void (*on_done)(void* ctx);

    double latitude;
    double longitude;

    curl_client_t* curl_client;

    char* buffer;
    int bytesread;

    weather_state state;
} weather_t;

typedef struct {
    // Location info
    double latitude;
    double longitude;
    double generationtime_ms;
    int utc_offset_seconds;
    char* timezone;
    char* timezone_abbreviation;
    double elevation;

    // Units
    char* unit_time;
    char* unit_interval;
    char* unit_temperature_2m;
    char* unit_relative_humidity_2m;
    char* unit_apparent_temperature;
    char* unit_is_day;
    char* unit_precipitation;
    char* unit_rain;
    char* unit_showers;
    char* unit_snowfall;
    char* unit_weather_code;
    char* unit_cloud_cover;
    char* unit_pressure_msl;
    char* unit_surface_pressure;
    char* unit_wind_speed_10m;
    char* unit_wind_direction_10m;
    char* unit_wind_gusts_10m;

    // Current weather data
    char* time;
    int interval;
    double temperature_2m;
    int relative_humidity_2m;
    double apparent_temperature;
    int is_day;
    double precipitation;
    double rain;
    double showers;
    double snowfall;
    int weather_code;
    int cloud_cover;
    double pressure_msl;
    double surface_pressure;
    double wind_speed_10m;
    int wind_direction_10m;
    double wind_gusts_10m;
} weather_data_t;

int weather_init(void** ctx, void** ctx_struct, void (*ondone)(void* context));
int weather_get_buffer(void** ctx, char** buffer);
int weather_work(void** ctx);
int weather_dispose(void** ctx);

int weather_set_location(void** ctx, double latitude, double longitude);

// ========== Cache Management Functions ==========
int does_weather_cache_exist(double latitude, double longitude);
int is_weather_cache_stale(double latitude, double longitude, int max_age_seconds);
int weather_cache_time(double latitude, double longitude, long int* time);
int load_weather_from_cache(double latitude, double longitude, char** json_str);
int save_weather_to_cache(double latitude, double longitude, const char* json_str);

// ========== API Fetching Functions ==========
int fetch_weather_from_openmeteo(double latitude, double longitude, char** api_response);

// ========== Serialization Functions ==========
// Used by server to process API response
int process_openmeteo_response(const char* api_response, char** client_response);
// Used by client to parse response
int deserialize_weather_response(const char* client_response, weather_data_t* weather);

// ========== Printing ==========
int weather_print(const weather_data_t* weather);
int weather_print_pretty(const weather_data_t* weather);

// ========== Memory Management ==========
void free_weather(weather_data_t* weather);

#endif // WEATHER_H
#include <ctype.h>
#include <math.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "utils.h"
#include "weather.h"

#define CACHE_DIR "weather_cache"

int parse_openmeteo_json_to_weather(const json_t* json_obj, weather_data_t* weather);
int serialize_weather_to_json(const weather_data_t* weather, json_t** json_obj);

static void get_cache_file_path(double latitude, double longitude, char* path, size_t path_size) {
    long long lat_key = llround(latitude * 1000000.0);
    long long lon_key = llround(longitude * 1000000.0);
    snprintf(path, path_size, "weather_cache/%lld_%lld.json", lat_key, lon_key);
}

int does_weather_cache_exist(double latitude, double longitude) {
    create_folder(CACHE_DIR);

    char cache_path[512];
    get_cache_file_path(latitude, longitude, cache_path, sizeof(cache_path));

    json_error_t error;
    json_t* json_obj = json_load_file(cache_path, 0, &error);

    if (json_obj) {
        json_decref(json_obj);
        return 0;
    }

    return -1;
}

int is_weather_cache_stale(double latitude, double longitude, int max_age_seconds) {
    if (max_age_seconds < 0) return 1;

    char cache_path[512];
    get_cache_file_path(latitude, longitude, cache_path, sizeof(cache_path));

    struct stat file_stat;
    if (stat(cache_path, &file_stat) != 0) { return -1; }

    time_t current_time = time(NULL);
    time_t file_age = current_time - file_stat.st_mtime;

    return file_age > max_age_seconds ? 1 : 0;
}

int weather_cache_time(double latitude, double longitude, long int* time) {
    if (!time) return -1;

    char cache_path[512];
    get_cache_file_path(latitude, longitude, cache_path, sizeof(cache_path));

    struct stat file_stat;
    if (stat(cache_path, &file_stat) != 0) { return -1; }

    *time = file_stat.st_mtime;

    return 0;
}

int load_weather_from_cache(double latitude, double longitude, char** json_str) {
    if (!json_str) return -1;

    char cache_path[512];
    get_cache_file_path(latitude, longitude, cache_path, sizeof(cache_path));

    json_error_t error;
    json_t* json_obj = json_load_file(cache_path, 0, &error);
    if (!json_obj) { return -1; }

    *json_str = json_dumps(json_obj, JSON_COMPACT);
    json_decref(json_obj);

    return *json_str ? 0 : -1;
}

int save_weather_to_cache(double latitude, double longitude, const char* json_str) {
    if (!json_str) return -1;

    char cache_path[512];
    get_cache_file_path(latitude, longitude, cache_path, sizeof(cache_path));

    json_error_t error;
    json_t* json_obj = json_loads(json_str, 0, &error);
    if (!json_obj) { return -1; }

    int result = json_dump_file(json_obj, cache_path, JSON_INDENT(2));
    json_decref(json_obj);

    return result == 0 ? 0 : -1;
}

int write_memory_callback(void* contents, size_t size, size_t nmemb, void* user_p) {
    size_t real_size = size * nmemb;

    struct memory_struct* mem = (struct memory_struct*)user_p;

    char* ptr = realloc(mem->memory, mem->size + real_size + 1);
    if (ptr == NULL) { return 0; }

    mem->memory = ptr;
    memcpy(&(mem->memory[mem->size]), contents, real_size);
    mem->size += real_size;
    mem->memory[mem->size] = 0;

    return real_size;
}

int fetch_weather_from_openmeteo(double latitude, double longitude, char** api_response) {
    CURL* curl;
    CURLcode res;

    struct memory_struct chunk;
    chunk.memory = malloc(1);
    chunk.size = 0;

    curl_global_init(CURL_GLOBAL_DEFAULT);
    curl = curl_easy_init();
    if (!curl) {
        free(chunk.memory);
        return -1;
    }

    char url[256];
    snprintf(url, sizeof(url), METEO_FORECAST_URL, latitude, longitude);
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_memory_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&chunk);
    res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        curl_easy_cleanup(curl);
        free(chunk.memory);
        return -1;
    }

    curl_easy_cleanup(curl);
    *api_response = chunk.memory;

    return 0;
}

int openmeteo_init(weather_t** weather) {
    CURLcode res = curl_global_init(CURL_GLOBAL_DEFAULT);
    if (res != CURLE_OK) { return -1; }

    (*weather)->curl_handle = curl_easy_init();
    if (!(*weather)->curl_handle) { return -1; }

    (*weather)->curl_multi_handle = curl_multi_init();
    if (!(*weather)->curl_multi_handle) {
        curl_easy_cleanup((*weather)->curl_handle);
        return -1;
    }

    /* Initialize memory buffer for the multi interface and set write callback */
    (*weather)->mem.memory = malloc(1);
    if (!(*weather)->mem.memory) {
        curl_multi_cleanup((*weather)->curl_multi_handle);
        curl_easy_cleanup((*weather)->curl_handle);
        return -1;
    }
    (*weather)->mem.size = 0;

    curl_easy_setopt((*weather)->curl_handle, CURLOPT_WRITEFUNCTION, write_memory_callback);
    curl_easy_setopt((*weather)->curl_handle, CURLOPT_WRITEDATA, (void*)&((*weather)->mem));

    return 0;
}

int openmeteo_make_request(weather_t** weather) {
    char url[256];
    snprintf(url, sizeof(url), METEO_FORECAST_URL, (*weather)->latitude, (*weather)->longitude);
    curl_easy_setopt((*weather)->curl_handle, CURLOPT_URL, url);
    curl_multi_add_handle((*weather)->curl_multi_handle, (*weather)->curl_handle);

    return 0;
}

int openmeteo_poll(weather_t** weather) {
    CURLMcode mc = curl_multi_perform((*weather)->curl_multi_handle, &(*weather)->curl_still_running);
    if (mc != CURLM_OK) { return -1; }

    return 0;
}

int openmeteo_read_response(weather_t** weather) {
    /* After multi perform indicates transfer complete, the data will be in weather->mem */
    if ((*weather)->mem.memory == NULL) return -1;

    (*weather)->buffer = (*weather)->mem.memory;
    (*weather)->bytesread = (int)(*weather)->mem.size;

    /* Take ownership of mem memory; prevent double-free */
    (*weather)->mem.memory = NULL;
    (*weather)->mem.size = 0;

    /* Remove the easy handle from the multi handle now that we're done with it */
    curl_multi_remove_handle((*weather)->curl_multi_handle, (*weather)->curl_handle);

    return 0;
}

int openmeteo_cleanup(weather_t** weather) {
    curl_easy_cleanup((*weather)->curl_handle);
    curl_multi_cleanup((*weather)->curl_multi_handle);
    curl_global_cleanup();

    return 0;
}

int process_openmeteo_response(const char* api_response, char** client_response) {
    weather_data_t weather;

    json_error_t error;
    json_t* root_api = json_loads(api_response, 0, &error);
    if (!root_api) return -1;

    int result = parse_openmeteo_json_to_weather(root_api, &weather);
    json_decref(root_api);
    if (result != 0) return -1;

    json_t* root_client;
    if (serialize_weather_to_json(&weather, &root_client) != 0) {
        free_weather(&weather);
        return -1;
    }
    *client_response = json_dumps(root_client, JSON_COMPACT);
    json_decref(root_client);

    free_weather(&weather);
    return 0;
}

int deserialize_weather_response(const char* client_response, weather_data_t* weather) {
    json_error_t error;
    json_t* root = json_loads(client_response, 0, &error);
    if (!root) return -1;

    int result = parse_openmeteo_json_to_weather(root, weather);
    json_decref(root);
    if (result != 0) return -1;

    return 0;
}

int weather_print(const weather_data_t* weather) {
    if (!weather) return -1;

    printf("Weather Data:\n");
    printf("  Location: (%.6f, %.6f)\n", weather->latitude, weather->longitude);
    printf("  Timezone: %s\n", weather->timezone ? weather->timezone : "N/A");
    printf("  Elevation: %.2f m\n", weather->elevation);
    printf("  Current Temperature: %.2f %s\n", weather->temperature_2m, weather->unit_temperature_2m ? weather->unit_temperature_2m : "N/A");
    printf("  Relative Humidity: %d %s\n", weather->relative_humidity_2m, weather->unit_relative_humidity_2m ? weather->unit_relative_humidity_2m : "N/A");
    printf("  Apparent Temperature: %.2f %s\n", weather->apparent_temperature, weather->unit_apparent_temperature ? weather->unit_apparent_temperature : "N/A");
    printf("  Is Day: %d %s\n", weather->is_day, weather->unit_is_day ? weather->unit_is_day : "N/A");
    printf("  Precipitation: %.2f %s\n", weather->precipitation, weather->unit_precipitation ? weather->unit_precipitation : "N/A");
    printf("  Wind Speed: %.2f %s\n", weather->wind_speed_10m, weather->unit_wind_speed_10m ? weather->unit_wind_speed_10m : "N/A");
    return 0;
}

int weather_print_pretty(const weather_data_t* weather) {
    if (!weather) return -1;
    printf("----- Weather Report -----\n");
    printf("Location: (%.6f, %.6f)\n", weather->latitude, weather->longitude);
    printf("Timezone: %s\n", weather->timezone ? weather->timezone : "N/A");
    printf("Elevation: %.2f m\n", weather->elevation);
    printf("Current Temperature: %.2f %s\n", weather->temperature_2m, weather->unit_temperature_2m ? weather->unit_temperature_2m : "N/A");
    printf("Relative Humidity: %d %s\n", weather->relative_humidity_2m, weather->unit_relative_humidity_2m ? weather->unit_relative_humidity_2m : "N/A");
    printf("Apparent Temperature: %.2f %s\n", weather->apparent_temperature, weather->unit_apparent_temperature ? weather->unit_apparent_temperature : "N/A");
    printf("Is Day: %d %s\n", weather->is_day, weather->unit_is_day ? weather->unit_is_day : "N/A");
    printf("Precipitation: %.2f %s\n", weather->precipitation, weather->unit_precipitation ? weather->unit_precipitation : "N/A");
    printf("Wind Speed: %.2f %s\n", weather->wind_speed_10m, weather->unit_wind_speed_10m ? weather->unit_wind_speed_10m : "N/A");
    printf("--------------------------\n");
    return 0;
}

int parse_openmeteo_json_to_weather(const json_t* json_obj, weather_data_t* weather) {
    if (!json_obj || !weather) return -1;

    memset(weather, 0, sizeof(weather_data_t));

    json_t* val;

    val = json_object_get(json_obj, "latitude");
    weather->latitude = json_is_real(val) ? json_real_value(val) : 0.0;

    val = json_object_get(json_obj, "longitude");
    weather->longitude = json_is_real(val) ? json_real_value(val) : 0.0;

    val = json_object_get(json_obj, "generationtime_ms");
    weather->generationtime_ms = json_is_real(val) ? json_real_value(val) : 0.0;

    val = json_object_get(json_obj, "utc_offset_seconds");
    weather->utc_offset_seconds = json_is_integer(val) ? (int)json_integer_value(val) : 0;

    val = json_object_get(json_obj, "timezone");
    weather->timezone = (json_is_string(val) && json_string_value(val)) ? strdup(json_string_value(val)) : NULL;

    val = json_object_get(json_obj, "timezone_abbreviation");
    weather->timezone_abbreviation = (json_is_string(val) && json_string_value(val)) ? strdup(json_string_value(val)) : NULL;

    val = json_object_get(json_obj, "elevation");
    weather->elevation = json_is_real(val) ? json_real_value(val) : 0.0;

    json_t* units_obj = json_object_get(json_obj, "current_units");
    if (json_is_object(units_obj)) {
        val = json_object_get(units_obj, "time");
        weather->unit_time = (json_is_string(val) && json_string_value(val)) ? strdup(json_string_value(val)) : NULL;

        val = json_object_get(units_obj, "interval");
        weather->unit_interval = (json_is_string(val) && json_string_value(val)) ? strdup(json_string_value(val)) : NULL;

        val = json_object_get(units_obj, "temperature_2m");
        weather->unit_temperature_2m = (json_is_string(val) && json_string_value(val)) ? strdup(json_string_value(val)) : NULL;

        val = json_object_get(units_obj, "relative_humidity_2m");
        weather->unit_relative_humidity_2m = (json_is_string(val) && json_string_value(val)) ? strdup(json_string_value(val)) : NULL;

        val = json_object_get(units_obj, "apparent_temperature");
        weather->unit_apparent_temperature = (json_is_string(val) && json_string_value(val)) ? strdup(json_string_value(val)) : NULL;

        val = json_object_get(units_obj, "is_day");
        weather->unit_is_day = (json_is_string(val) && json_string_value(val)) ? strdup(json_string_value(val)) : NULL;

        val = json_object_get(units_obj, "precipitation");
        weather->unit_precipitation = (json_is_string(val) && json_string_value(val)) ? strdup(json_string_value(val)) : NULL;

        val = json_object_get(units_obj, "rain");
        weather->unit_rain = (json_is_string(val) && json_string_value(val)) ? strdup(json_string_value(val)) : NULL;

        val = json_object_get(units_obj, "showers");
        weather->unit_showers = (json_is_string(val) && json_string_value(val)) ? strdup(json_string_value(val)) : NULL;

        val = json_object_get(units_obj, "snowfall");
        weather->unit_snowfall = (json_is_string(val) && json_string_value(val)) ? strdup(json_string_value(val)) : NULL;

        val = json_object_get(units_obj, "weather_code");
        weather->unit_weather_code = (json_is_string(val) && json_string_value(val)) ? strdup(json_string_value(val)) : NULL;

        val = json_object_get(units_obj, "cloud_cover");
        weather->unit_cloud_cover = (json_is_string(val) && json_string_value(val)) ? strdup(json_string_value(val)) : NULL;

        val = json_object_get(units_obj, "pressure_msl");
        weather->unit_pressure_msl = (json_is_string(val) && json_string_value(val)) ? strdup(json_string_value(val)) : NULL;

        val = json_object_get(units_obj, "surface_pressure");
        weather->unit_surface_pressure = (json_is_string(val) && json_string_value(val)) ? strdup(json_string_value(val)) : NULL;

        val = json_object_get(units_obj, "wind_speed_10m");
        weather->unit_wind_speed_10m = (json_is_string(val) && json_string_value(val)) ? strdup(json_string_value(val)) : NULL;

        val = json_object_get(units_obj, "wind_direction_10m");
        weather->unit_wind_direction_10m = (json_is_string(val) && json_string_value(val)) ? strdup(json_string_value(val)) : NULL;

        val = json_object_get(units_obj, "wind_gusts_10m");
        weather->unit_wind_gusts_10m = (json_is_string(val) && json_string_value(val)) ? strdup(json_string_value(val)) : NULL;
    }

    json_t* current_obj = json_object_get(json_obj, "current");
    if (json_is_object(current_obj)) {
        val = json_object_get(current_obj, "time");
        weather->time = (json_is_string(val) && json_string_value(val)) ? strdup(json_string_value(val)) : NULL;

        val = json_object_get(current_obj, "interval");
        weather->interval = json_is_integer(val) ? (int)json_integer_value(val) : 0;

        val = json_object_get(current_obj, "temperature_2m");
        weather->temperature_2m = json_is_real(val) ? json_real_value(val) : (json_is_integer(val) ? (double)json_integer_value(val) : 0.0);

        val = json_object_get(current_obj, "relative_humidity_2m");
        weather->relative_humidity_2m = json_is_integer(val) ? (int)json_integer_value(val) : (json_is_real(val) ? (int)json_real_value(val) : 0);

        val = json_object_get(current_obj, "apparent_temperature");
        weather->apparent_temperature = json_is_real(val) ? json_real_value(val) : (json_is_integer(val) ? (double)json_integer_value(val) : 0.0);

        val = json_object_get(current_obj, "is_day");
        weather->is_day = json_is_integer(val) ? (int)json_integer_value(val) : 0;

        val = json_object_get(current_obj, "precipitation");
        weather->precipitation = json_is_real(val) ? json_real_value(val) : (json_is_integer(val) ? (double)json_integer_value(val) : 0.0);

        val = json_object_get(current_obj, "rain");
        weather->rain = json_is_real(val) ? json_real_value(val) : (json_is_integer(val) ? (double)json_integer_value(val) : 0.0);

        val = json_object_get(current_obj, "showers");
        weather->showers = json_is_real(val) ? json_real_value(val) : (json_is_integer(val) ? (double)json_integer_value(val) : 0.0);

        val = json_object_get(current_obj, "snowfall");
        weather->snowfall = json_is_real(val) ? json_real_value(val) : (json_is_integer(val) ? (double)json_integer_value(val) : 0.0);

        val = json_object_get(current_obj, "weather_code");
        weather->weather_code = json_is_integer(val) ? (int)json_integer_value(val) : (json_is_real(val) ? (int)json_real_value(val) : 0);

        val = json_object_get(current_obj, "cloud_cover");
        weather->cloud_cover = json_is_integer(val) ? (int)json_integer_value(val) : (json_is_real(val) ? (int)json_real_value(val) : 0);

        val = json_object_get(current_obj, "pressure_msl");
        weather->pressure_msl = json_is_real(val) ? json_real_value(val) : (json_is_integer(val) ? (double)json_integer_value(val) : 0.0);

        val = json_object_get(current_obj, "surface_pressure");
        weather->surface_pressure = json_is_real(val) ? json_real_value(val) : (json_is_integer(val) ? (double)json_integer_value(val) : 0.0);

        val = json_object_get(current_obj, "wind_speed_10m");
        weather->wind_speed_10m = json_is_real(val) ? json_real_value(val) : (json_is_integer(val) ? (double)json_integer_value(val) : 0.0);

        val = json_object_get(current_obj, "wind_direction_10m");
        weather->wind_direction_10m = json_is_integer(val) ? (int)json_integer_value(val) : (json_is_real(val) ? (int)json_real_value(val) : 0);

        val = json_object_get(current_obj, "wind_gusts_10m");
        weather->wind_gusts_10m = json_is_real(val) ? json_real_value(val) : (json_is_integer(val) ? (double)json_integer_value(val) : 0.0);
    }

    return 0;
}

int serialize_weather_to_json(const weather_data_t* weather, json_t** json_obj) {
    if (!weather || !json_obj) return -1;

    *json_obj = json_object();
    if (!*json_obj) return -1;

    json_object_set_new(*json_obj, "latitude", json_real(weather->latitude));
    json_object_set_new(*json_obj, "longitude", json_real(weather->longitude));
    json_object_set_new(*json_obj, "generationtime_ms", json_real(weather->generationtime_ms));
    json_object_set_new(*json_obj, "utc_offset_seconds", json_integer(weather->utc_offset_seconds));
    json_object_set_new(*json_obj, "timezone", weather->timezone ? json_string(weather->timezone) : json_string("GMT"));
    json_object_set_new(*json_obj, "timezone_abbreviation", weather->timezone_abbreviation ? json_string(weather->timezone_abbreviation) : json_string("GMT"));
    json_object_set_new(*json_obj, "elevation", json_real(weather->elevation));

    json_t* units_obj = json_object();
    if (!units_obj) {
        json_decref(*json_obj);
        return -1;
    }
    json_object_set_new(units_obj, "time", weather->unit_time ? json_string(weather->unit_time) : json_string("iso8601"));
    json_object_set_new(units_obj, "interval", weather->unit_interval ? json_string(weather->unit_interval) : json_string("seconds"));
    json_object_set_new(units_obj, "temperature_2m", weather->unit_temperature_2m ? json_string(weather->unit_temperature_2m) : json_string("°C"));
    json_object_set_new(units_obj, "relative_humidity_2m",
                        weather->unit_relative_humidity_2m ? json_string(weather->unit_relative_humidity_2m) : json_string("%"));
    json_object_set_new(units_obj, "apparent_temperature",
                        weather->unit_apparent_temperature ? json_string(weather->unit_apparent_temperature) : json_string("°C"));
    json_object_set_new(units_obj, "is_day", weather->unit_is_day ? json_string(weather->unit_is_day) : json_string(""));
    json_object_set_new(units_obj, "precipitation", weather->unit_precipitation ? json_string(weather->unit_precipitation) : json_string("mm"));
    json_object_set_new(units_obj, "rain", weather->unit_rain ? json_string(weather->unit_rain) : json_string("mm"));
    json_object_set_new(units_obj, "showers", weather->unit_showers ? json_string(weather->unit_showers) : json_string("mm"));
    json_object_set_new(units_obj, "snowfall", weather->unit_snowfall ? json_string(weather->unit_snowfall) : json_string("cm"));
    json_object_set_new(units_obj, "weather_code", weather->unit_weather_code ? json_string(weather->unit_weather_code) : json_string("wmo code"));
    json_object_set_new(units_obj, "cloud_cover", weather->unit_cloud_cover ? json_string(weather->unit_cloud_cover) : json_string("%"));
    json_object_set_new(units_obj, "pressure_msl", weather->unit_pressure_msl ? json_string(weather->unit_pressure_msl) : json_string("hPa"));
    json_object_set_new(units_obj, "surface_pressure", weather->unit_surface_pressure ? json_string(weather->unit_surface_pressure) : json_string("hPa"));
    json_object_set_new(units_obj, "wind_speed_10m", weather->unit_wind_speed_10m ? json_string(weather->unit_wind_speed_10m) : json_string("km/h"));
    json_object_set_new(units_obj, "wind_direction_10m", weather->unit_wind_direction_10m ? json_string(weather->unit_wind_direction_10m) : json_string("°"));
    json_object_set_new(units_obj, "wind_gusts_10m", weather->unit_wind_gusts_10m ? json_string(weather->unit_wind_gusts_10m) : json_string("km/h"));
    json_object_set_new(*json_obj, "current_units", units_obj);

    json_t* current_obj = json_object();
    if (!current_obj) {
        json_decref(*json_obj);
        return -1;
    }
    json_object_set_new(current_obj, "time", weather->time ? json_string(weather->time) : json_null());
    json_object_set_new(current_obj, "interval", json_integer(weather->interval));
    json_object_set_new(current_obj, "temperature_2m", json_real(weather->temperature_2m));
    json_object_set_new(current_obj, "relative_humidity_2m", json_integer(weather->relative_humidity_2m));
    json_object_set_new(current_obj, "apparent_temperature", json_real(weather->apparent_temperature));
    json_object_set_new(current_obj, "is_day", json_integer(weather->is_day));
    json_object_set_new(current_obj, "precipitation", json_real(weather->precipitation));
    json_object_set_new(current_obj, "rain", json_real(weather->rain));
    json_object_set_new(current_obj, "showers", json_real(weather->showers));
    json_object_set_new(current_obj, "snowfall", json_real(weather->snowfall));
    json_object_set_new(current_obj, "weather_code", json_integer(weather->weather_code));
    json_object_set_new(current_obj, "cloud_cover", json_integer(weather->cloud_cover));
    json_object_set_new(current_obj, "pressure_msl", json_real(weather->pressure_msl));
    json_object_set_new(current_obj, "surface_pressure", json_real(weather->surface_pressure));
    json_object_set_new(current_obj, "wind_speed_10m", json_real(weather->wind_speed_10m));
    json_object_set_new(current_obj, "wind_direction_10m", json_integer(weather->wind_direction_10m));
    json_object_set_new(current_obj, "wind_gusts_10m", json_real(weather->wind_gusts_10m));
    json_object_set_new(*json_obj, "current", current_obj);

    return 0;
}

void free_weather(weather_data_t* weather) {
    if (!weather) return;

    free(weather->timezone);
    free(weather->timezone_abbreviation);
    free(weather->unit_time);
    free(weather->unit_interval);
    free(weather->unit_temperature_2m);
    free(weather->unit_relative_humidity_2m);
    free(weather->unit_apparent_temperature);
    free(weather->unit_is_day);
    free(weather->unit_precipitation);
    free(weather->unit_rain);
    free(weather->unit_showers);
    free(weather->unit_snowfall);
    free(weather->unit_weather_code);
    free(weather->unit_cloud_cover);
    free(weather->unit_pressure_msl);
    free(weather->unit_surface_pressure);
    free(weather->unit_wind_speed_10m);
    free(weather->unit_wind_direction_10m);
    free(weather->unit_wind_gusts_10m);
    free(weather->time);

    memset(weather, 0, sizeof(weather_data_t));
}

int weather_init(void** ctx, void** ctx_struct, void (*ondone)(void* context)) {
    weather_t* weather = (weather_t*)malloc(sizeof(weather_t));
    if (!weather) { return -1; }
    memset(weather, 0, sizeof(weather_t));
    weather->ctx = ctx;
    weather->state = Weather_State_Init;
    weather->buffer = NULL;
    weather->bytesread = 0;
    weather->on_done = ondone;
    *ctx_struct = (void*)weather;

    return 0;
}

int weather_get_buffer(void** ctx, char** buffer) {
    weather_t* weather = (weather_t*)(*ctx);
    if (!weather) { return -1; }
    *buffer = weather->buffer;
    return 0;
}

int weather_work(void** ctx) {
    weather_t* weather = (weather_t*)(*ctx);
    if (!weather) { return -1; }

    switch (weather->state) {
    case Weather_State_Init:
        create_folder(CACHE_DIR);
        weather->state = Weather_State_ValidateFile;
        printf("Weather: Initialized\n");
        break;
    case Weather_State_ValidateFile:
        if (does_weather_cache_exist(weather->latitude, weather->longitude) == 0 && is_weather_cache_stale(weather->latitude, weather->longitude, 900) == 0) {
            weather->state = Weather_State_LoadFromDisk;
        } else {
            weather->state = Weather_State_FetchFromAPI_Init;
        }
        printf("Weather: Validating File\n");
        break;
    case Weather_State_LoadFromDisk:
        char* json_str = NULL;
        if (load_weather_from_cache(weather->latitude, weather->longitude, &json_str) == 0) {
            weather->buffer = json_str;
        } else {
            weather->buffer = NULL;
        }
        weather->state = Weather_State_Done;
        printf("Weather: Loading From Disk\n");
        break;
    case Weather_State_FetchFromAPI_Init:
        if (openmeteo_init(&weather) != 0) {
            weather->state = Weather_State_Done;
            break;
        }
        weather->state = Weather_State_FetchFromAPI_Request;
        printf("Weather: Fetching From API\n");
        break;
    case Weather_State_FetchFromAPI_Request:
        if (openmeteo_make_request(&weather) != 0) {
            weather->state = Weather_State_Done;
            break;
        }
        weather->state = Weather_State_FetchFromAPI_Poll;
        break;
    case Weather_State_FetchFromAPI_Poll:
        if (openmeteo_poll(&weather) != 0) {
            weather->state = Weather_State_Done;
            break;
        }
        if (weather->curl_still_running) {
            break;
        } else {
            weather->state = Weather_State_FetchFromAPI_Read;
        }
        break;
    case Weather_State_FetchFromAPI_Read:
        if (openmeteo_read_response(&weather) != 0) {
            weather->state = Weather_State_Done;
            break;
        }
        openmeteo_cleanup(&weather);
        weather->state = Weather_State_ProcessResponse;
        break;
    case Weather_State_ProcessResponse:
        char* client_response = NULL;
        if (process_openmeteo_response(weather->buffer, &client_response) != 0) {
            weather->state = Weather_State_Done;
            printf("Weather: Processing Response Failed\n");
        } else {
            free(weather->buffer);
            weather->buffer = client_response;
            weather->state = Weather_State_SaveToDisk;
            printf("Weather: Processing Response Succeeded\n");
        }
        break;
    case Weather_State_SaveToDisk:
        if (save_weather_to_cache(weather->latitude, weather->longitude, weather->buffer) != 0) { printf("Weather: Saving To Disk Failed\n"); }
        weather->state = Weather_State_Done;
        break;
    case Weather_State_Done:
        weather->on_done(weather->ctx);
        printf("Weather: Done\n");
        break;
    }

    return 0;
}

int weather_dispose(void** ctx) {
    weather_t* weather = (weather_t*)(*ctx);
    if (!weather) return -1;

    if (weather->mem.memory) free(weather->mem.memory);
    free(weather->buffer);
    free(weather);
    *ctx = NULL;

    return 0;
}

int weather_set_location(void** ctx, double latitude, double longitude) {
    weather_t* weather = (weather_t*)(*ctx);
    if (!weather) return -1;
    weather->latitude = latitude;
    weather->longitude = longitude;

    return 0;
}

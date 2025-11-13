#include "cities_new.h"

#include "tinydir.h"
#include "utils.h"
#include <jansson.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CACHE_DIR "cities_cache"

const char* cities_list = "Stockholm:59.3293:18.0686\n"
                          "Göteborg:57.7089:11.9746\n"
                          "Malmö:55.6050:13.0038\n"
                          "Uppsala:59.8586:17.6389\n"
                          "Västerås:59.6099:16.5448\n"
                          "Örebro:59.2741:15.2066\n"
                          "Linköping:58.4109:15.6216\n"
                          "Helsingborg:56.0465:12.6945\n"
                          "Jönköping:57.7815:14.1562\n"
                          "Norrköping:58.5877:16.1924\n"
                          "Lund:55.7047:13.1910\n"
                          "Gävle:60.6749:17.1413\n"
                          "Sundsvall:62.3908:17.3069\n"
                          "Umeå:63.8258:20.2630\n"
                          "Luleå:65.5848:22.1567\n"
                          "Kiruna:67.8558:20.2253\n";

// Forward declarations

int city_init(const char* _Name, const char* _Latitude, const char* _Longitude, city_t** _CityPtr);
void city_dispose(city_t** _cityPtr);

int cities_add_city(cities_t* cities, city_t* city);

int cities_load_from_disk(cities_t* cities);
int cities_read_from_string_list(cities_t* cities);

int cities_convert_to_char_json_buffer(cities_t* cities);

// Function implementations

int cities_init(void** ctx, void(*ondone)) {
    cities_t* cities = (cities_t*)malloc(sizeof(cities_t));
    if (!cities) {
        return -1; // Memory allocation failed
    }
    memset(&cities->cities_list, 0, sizeof(LinkedList));
    cities->state = Cities_State_Init;
    cities->buffer = NULL;
    cities->bytesread = 0;
    cities->on_done = ondone;
    *ctx = (void*)cities;

    return 0;
}

int city_init(const char* _Name, const char* _Latitude, const char* _Longitude, city_t** _CityPtr) {
    if (_Name == NULL || _CityPtr == NULL) return -1;

    city_t* _city = (city_t*)malloc(sizeof(city_t));
    if (_city == NULL) return -1;

    memset(_city, 0, sizeof(city_t));

    _city->name = strdup(_Name);
    if (_city->name == NULL) {
        free(_city);
        return -1;
    }

    if (_Latitude != NULL) {
        _city->latitude = atof(_Latitude);
    } else {
        _city->latitude = 0.0f;
    }

    if (_Longitude != NULL) {
        _city->latitude = atof(_Longitude);
    } else {
        _city->latitude = 0.0f;
    }

    *(_CityPtr) = _city;

    return 0;
}

void city_dispose(city_t** _cityPtr) {
    if (_cityPtr == NULL || *_cityPtr == NULL) return;

    city_t* _city = *_cityPtr;

    if (_city->name != NULL) free(_city->name);

    free(_city);
    *_cityPtr = NULL;
}

int cities_add_city(cities_t* cities, city_t* city) {
    LinkedList_append(&cities->cities_list, city);
    return 0;
}

int cities_get_city_by_name(cities_t* cities, const char* name, city_t** city_ptr) {
    if (!cities || !name) return -1;

    Node* node = cities->cities_list.head;
    while (node) {
        city_t* city = (city_t*)node->item;
        if (city && city->name && strcmp(city->name, name) == 0) {
            if (city_ptr) *city_ptr = city;
            return 0;
        }
        node = node->front;
    }

    return -1;
}

int cities_load_from_disk(cities_t* cities) {
    tinydir_dir dir;
    if (tinydir_open(&dir, CACHE_DIR) == -1) {
        return -1; // Failed to open directory
    }

    while (dir.has_next) {
        tinydir_file file;
        if (tinydir_readfile(&dir, &file) == -1) {
            tinydir_close(&dir);
            return -1;
        }

        if (!file.is_dir) {
            json_t* city_json = json_load_file(file.path, 0, NULL);
            if (city_json) {
                const char* name = json_string_value(json_object_get(city_json, "name"));
                double latitude = json_number_value(json_object_get(city_json, "latitude"));
                double longitude = json_number_value(json_object_get(city_json, "longitude"));

                char lat_buffer[32];
                char lon_buffer[32];
                snprintf(lat_buffer, sizeof(lat_buffer), "%.6f", latitude);
                snprintf(lon_buffer, sizeof(lon_buffer), "%.6f", longitude);

                city_t* city = NULL;
                city_init(name, lat_buffer, lon_buffer, &city);
                if (city) cities_add_city(cities, city);

                json_decref(city_json);
            }
        }

        if (tinydir_next(&dir) == -1) {
            tinydir_close(&dir);
            return -1;
        }
    }

    tinydir_close(&dir);

    return 0;
}

int cities_read_from_string_list(cities_t* cities) {
    if (!cities) return -1;

    char* list_copy = strdup(cities_list);
    if (!list_copy) return -1;

    char* ptr = list_copy;

    char* name = NULL;
    char* lat_str = NULL;
    char* lon_str = NULL;

    do {
        name = ptr;
        lat_str = strchr(ptr, ':');
        if (!lat_str) break;
        *lat_str = '\0';
        lat_str++;

        lon_str = strchr(lat_str, ':');
        if (!lon_str) break;
        *lon_str = '\0';
        lon_str++;

        char* end_ptr = strchr(lon_str, '\n');
        if (end_ptr) {
            *end_ptr = '\0';
            ptr = end_ptr + 1;
        } else {
            ptr = NULL;
        }

        city_t* existing_city = NULL;
        if (cities_get_city_by_name(cities, name, &existing_city) == 0) { continue; }

        city_t* city = NULL;

        city_init(name, lat_str, lon_str, &city);

        if (city) { cities_add_city(cities, city); }

    } while (ptr);

    return 0;
}

int cities_work(void** ctx) {
    cities_t* cities = (cities_t*)(*ctx);
    if (!cities) {
        return -1; // Memory allocation failed
    }

    switch (cities->state) {
    case Cities_State_Init:
        create_folder(CACHE_DIR);
        cities->state = Cities_State_ReadFiles;
        break;
    case Cities_State_ReadFiles:
        cities_load_from_disk(cities);
        cities->state = Cities_State_ReadString;
        break;
    case Cities_State_ReadString:
        cities_read_from_string_list(cities);
        cities->state = Cities_State_Convert;
        break;
    case Cities_State_Convert:
        break;
    case Cities_State_Done:
        break;
    }

    cities->buffer = "HELLO GARFALD!";
    cities->bytesread = strlen(cities->buffer);

    return 0;
}

int cities_convert_to_char_json_buffer(cities_t* cities) {
    if (!cities) return -1;

    json_t* root_array = json_array();
    if (!root_array) return -1;

    Node* node = cities->cities_list.head;
    while (node) {
        city_t* city = (city_t*)node->item;
        if (city) {
            json_t* city_obj = json_object();
            if (city_obj) {
                json_object_set_new(city_obj, "name", json_string(city->name));
                json_object_set_new(city_obj, "latitude", json_real(city->latitude));
                json_object_set_new(city_obj, "longitude", json_real(city->longitude));
                json_array_append_new(root_array, city_obj);
            }
        }
        node = node->front;
    }

    char* json_str = json_dumps(root_array, JSON_INDENT(2));
    if (!json_str) {
        json_decref(root_array);
        return -1;
    }

    cities->buffer = json_str;
    cities->bytesread = strlen(json_str);

    json_decref(root_array);

    return 0;
}

int cities_get_buffer(void** ctx, char** buffer) {
    cities_t* cities = (cities_t*)(*ctx);
    if (!cities) {
        return -1; // Memory allocation failed
    }
    *buffer = cities->buffer;
    return 0;
}

int cities_dispose(void** ctx) {
    cities_t* cities = (cities_t*)(*ctx);
    if (!cities) return -1; // Memory allocation failed

    return 0;
}

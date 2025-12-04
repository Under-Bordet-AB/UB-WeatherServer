#include "geolocation.h"

// Forward declaration

int min(int a, int b, int c) {
    return a < b ? (a < c ? a : c) : (b < c ? b : c);
}

int process_openmeteo_geo_response(const char* api_response, char** client_response);

// name can be "s"/"st"/"stoc"/"stockho"/"stockholm"
// ska vara fuzzy matching, liksom bara närhet i allmänhet, och ha minsta möjliga för att något ska bli en candidate
// om vi hittar 5 candidates, skicka tillbaks dem
// om vi hittar t.ex 3 candidates, hämta fler från openmeteo, kör igen, och skicka dem vi lyckas fixa

// int does_location_exist(char* name);

int levenshtein(const char *s1, const char *s2);
// int location_delta(char* nameA, char* nameB);

// int add_location_to_list(location_t* location);
int save_location_to_disk(location_t* location);

int look_for_candidates();

int levenshtein(const char *s1, const char *s2) {
    int len1 = strlen(s1), len2 = strlen(s2);
    int *prev = malloc((len2 + 1) * sizeof(int));
    int *curr = malloc((len2 + 1) * sizeof(int));

    for (int j = 0; j <= len2; j++)
        prev[j] = j;

    for (int i = 1; i <= len1; i++) {
        curr[0] = i;
        for (int j = 1; j <= len2; j++) {
            int cost = (s1[i-1] == s2[j-1]) ? 0 : 1;
            curr[j] = min(
                prev[j] + 1,      // deletion
                curr[j-1] + 1,    // insertion
                prev[j-1] + cost  // substitution
            );
        }
        int *temp = prev;
        prev = curr;
        curr = temp;
    }

    int distance = prev[len2];
    free(prev);
    free(curr);
    return distance;
}

// Cache functions:

int look_for_candidates(geolocation_t* geolocation) {
    tinydir_dir dir;
    if (tinydir_open(&dir, GEOLOCATIONS_CACHE_DIR) == -1) {
        return -1; // Failed to open directory
    }

    typedef struct loc_candidate_t {
        char* name;
        int distance;
    } loc_candidate_t;

    size_t capacity = 4;
    size_t count = 0;
    loc_candidate_t* candidates = malloc(capacity * sizeof(loc_candidate_t));
    if (!candidates) {
        tinydir_close(&dir);
        return -1;
    }

    while (dir.has_next) {
        tinydir_file file;
        if (tinydir_readfile(&dir, &file) == -1) {
            tinydir_close(&dir);
            free(candidates);
            return -1;
        }

        if (!file.is_dir) {
            json_t* location_json = json_load_file(file.path, 0, NULL);
            if (location_json) {
                const char* name = json_string_value(json_object_get(location_json, "name"));
                int distance = levenshtein(name, geolocation->location_name);
                if (distance <= 3) {  // Keep similar names (small distance)
                    if (count >= capacity) {  // Expand before running out of space
                        capacity *= 2;
                        loc_candidate_t* new_candidates = realloc(candidates, capacity * sizeof(loc_candidate_t));
                        if (!new_candidates) {
                            json_decref(location_json);
                            tinydir_close(&dir);
                            for (size_t i = 0; i < count; i++) {
                                free(candidates[i].name);
                            }
                            free(candidates);
                            return -1;
                        }
                        candidates = new_candidates;
                    }
                    loc_candidate_t candidate;
                    candidate.name = strdup(name);
                    candidate.distance = distance;
                    candidates[count] = candidate;
                    
                    count++;
                }

                json_decref(location_json);
            }
        }

        if (tinydir_next(&dir) == -1) {
            tinydir_close(&dir);
            for (size_t i = 0; i < count; i++) {
                free(candidates[i].name);
            }
            free(candidates);
            return -1;
        }
    }

    tinydir_close(&dir);

    // Sort candidates by distance (ascending - closest first)
    for (size_t i = 0; i < count - 1; i++) {
        for (size_t j = 0; j < count - i - 1; j++) {
            if (candidates[j].distance > candidates[j + 1].distance) {
                loc_candidate_t temp = candidates[j];
                candidates[j] = candidates[j + 1];
                candidates[j + 1] = temp;
            }
        }
    }

    // TODO: Store or use the candidates array
    // For now, just clean up
    for (size_t i = 0; i < count; i++) {
        free(candidates[i].name);
    }
    free(candidates);

    return 0;
}

// Openmeteo API functions:

int parse_openmeteo_geo_json_to_location(const json_t* json_obj, location_t* location) {
    if (!json_obj || !location) return -1;

    memset(location, 0, sizeof(location_t));

    json_t* val;

    val = json_object_get(json_obj, "id");
    location->id = json_is_integer(val) ? (int)json_integer_value(val) : 0;

    val = json_object_get(json_obj, "name");
    location->name = (json_is_string(val) && json_string_value(val)) ? strdup(json_string_value(val)) : NULL;

    val = json_object_get(json_obj, "latitude");
    location->latitude = json_is_real(val) ? json_real_value(val) : (json_is_integer(val) ? (double)json_integer_value(val) : 0.0);

    val = json_object_get(json_obj, "longitude");
    location->longitude = json_is_real(val) ? json_real_value(val) : (json_is_integer(val) ? (double)json_integer_value(val) : 0.0);

    val = json_object_get(json_obj, "elevation");
    location->elevation = json_is_real(val) ? json_real_value(val) : (json_is_integer(val) ? (double)json_integer_value(val) : 0.0);

    val = json_object_get(json_obj, "feature_code");
    location->feature_code = (json_is_string(val) && json_string_value(val)) ? strdup(json_string_value(val)) : NULL;

    val = json_object_get(json_obj, "country_code");
    location->country_code = (json_is_string(val) && json_string_value(val)) ? strdup(json_string_value(val)) : NULL;

    val = json_object_get(json_obj, "admin1_id");
    location->admin1_id = json_is_integer(val) ? (int)json_integer_value(val) : 0;

    val = json_object_get(json_obj, "admin2_id");
    location->admin2_id = json_is_integer(val) ? (int)json_integer_value(val) : 0;

    val = json_object_get(json_obj, "admin3_id");
    location->admin3_id = json_is_integer(val) ? (int)json_integer_value(val) : 0;

    val = json_object_get(json_obj, "admin4_id");
    location->admin4_id = json_is_integer(val) ? (int)json_integer_value(val) : 0;

    val = json_object_get(json_obj, "timezone");
    location->timezone = (json_is_string(val) && json_string_value(val)) ? strdup(json_string_value(val)) : NULL;

    val = json_object_get(json_obj, "population");
    location->population = json_is_integer(val) ? (int)json_integer_value(val) : 0;

    val = json_object_get(json_obj, "postcodes");
    if (json_is_array(val)) {
        size_t count = json_array_size(val);
        location->postcodes_count = count;
        location->postcodes = (char**)malloc(count * sizeof(char*));
        if (location->postcodes) {
            for (size_t i = 0; i < count; i++) {
                json_t* postcode = json_array_get(val, i);
                location->postcodes[i] = (json_is_string(postcode) && json_string_value(postcode)) 
                    ? strdup(json_string_value(postcode)) : NULL;
            }
        }
    }

    val = json_object_get(json_obj, "country_id");
    location->country_id = json_is_integer(val) ? (int)json_integer_value(val) : 0;

    val = json_object_get(json_obj, "country");
    location->country = (json_is_string(val) && json_string_value(val)) ? strdup(json_string_value(val)) : NULL;

    val = json_object_get(json_obj, "admin1");
    location->admin1 = (json_is_string(val) && json_string_value(val)) ? strdup(json_string_value(val)) : NULL;

    val = json_object_get(json_obj, "admin2");
    location->admin2 = (json_is_string(val) && json_string_value(val)) ? strdup(json_string_value(val)) : NULL;

    val = json_object_get(json_obj, "admin3");
    location->admin3 = (json_is_string(val) && json_string_value(val)) ? strdup(json_string_value(val)) : NULL;

    val = json_object_get(json_obj, "admin4");
    location->admin4 = (json_is_string(val) && json_string_value(val)) ? strdup(json_string_value(val)) : NULL;

    return 0;
}

int serialize_location_to_json(const location_t* location, json_t** json_obj) {
    if (!location || !json_obj) return -1;

    *json_obj = json_object();
    if (!*json_obj) return -1;

    json_object_set_new(*json_obj, "id", json_integer(location->id));
    json_object_set_new(*json_obj, "name", location->name ? json_string(location->name) : json_null());
    json_object_set_new(*json_obj, "latitude", json_real(location->latitude));
    json_object_set_new(*json_obj, "longitude", json_real(location->longitude));
    json_object_set_new(*json_obj, "elevation", json_real(location->elevation));
    json_object_set_new(*json_obj, "feature_code", location->feature_code ? json_string(location->feature_code) : json_null());
    json_object_set_new(*json_obj, "country_code", location->country_code ? json_string(location->country_code) : json_null());
    json_object_set_new(*json_obj, "admin1_id", json_integer(location->admin1_id));
    json_object_set_new(*json_obj, "admin2_id", json_integer(location->admin2_id));
    json_object_set_new(*json_obj, "admin3_id", json_integer(location->admin3_id));
    json_object_set_new(*json_obj, "admin4_id", json_integer(location->admin4_id));
    json_object_set_new(*json_obj, "timezone", location->timezone ? json_string(location->timezone) : json_null());
    json_object_set_new(*json_obj, "population", json_integer(location->population));

    if (location->postcodes && location->postcodes_count > 0) {
        json_t* postcodes_array = json_array();
        for (size_t i = 0; i < location->postcodes_count; i++) {
            if (location->postcodes[i]) {
                json_array_append_new(postcodes_array, json_string(location->postcodes[i]));
            }
        }
        json_object_set_new(*json_obj, "postcodes", postcodes_array);
    } else {
        json_object_set_new(*json_obj, "postcodes", json_array());
    }

    json_object_set_new(*json_obj, "country_id", json_integer(location->country_id));
    json_object_set_new(*json_obj, "country", location->country ? json_string(location->country) : json_null());
    json_object_set_new(*json_obj, "admin1", location->admin1 ? json_string(location->admin1) : json_null());
    json_object_set_new(*json_obj, "admin2", location->admin2 ? json_string(location->admin2) : json_null());
    json_object_set_new(*json_obj, "admin3", location->admin3 ? json_string(location->admin3) : json_null());
    json_object_set_new(*json_obj, "admin4", location->admin4 ? json_string(location->admin4) : json_null());

    return 0;
}

int process_openmeteo_geo_response(const char* api_response, char** client_response) {
    json_error_t error;
    json_t* root_api = json_loads(api_response, 0, &error);
    if (!root_api) return -1;

    // The API returns {"results": [array of locations]}
    json_t* results_array = json_object_get(root_api, "results");
    if (!json_is_array(results_array) || json_array_size(results_array) == 0) {
        json_decref(root_api);
        return -1;
    }

    // Create an array to hold all location results
    json_t* client_array = json_array();
    if (!client_array) {
        json_decref(root_api);
        return -1;
    }

    // Process all results
    size_t num_results = json_array_size(results_array);
    for (size_t i = 0; i < num_results; i++) {
        json_t* result_item = json_array_get(results_array, i);
        if (!result_item) continue;

        location_t location;
        if (parse_openmeteo_geo_json_to_location(result_item, &location) == 0) {
            json_t* location_json;
            if (serialize_location_to_json(&location, &location_json) == 0) {
                json_array_append_new(client_array, location_json);
            }
            free_location(&location);
        }
    }

    *client_response = json_dumps(client_array, JSON_COMPACT);
    json_decref(client_array);
    json_decref(root_api);
    
    return 0;
}

// Memory management:

int save_location_to_disk(location_t* location) {
    if (location == NULL) return -1;
    json_t* json_obj;
    serialize_location_to_json(location, &json_obj);

    int result = json_dump_file(json_obj, GEOLOCATIONS_CACHE_DIR, JSON_INDENT(2));
    json_decref(json_obj);

    return result;
}

void free_location(location_t* location) {
    if (!location) return;

    free(location->name);
    free(location->feature_code);
    free(location->country_code);
    free(location->timezone);
    free(location->country);
    free(location->admin1);
    free(location->admin2);
    free(location->admin3);
    free(location->admin4);

    if (location->postcodes) {
        for (size_t i = 0; i < location->postcodes_count; i++) {
            free(location->postcodes[i]);
        }
        free(location->postcodes);
    }

    memset(location, 0, sizeof(location_t));
}

// Basics:

int geolocation_set_parameters(void** ctx, char* location_name, int location_count, char* country_code) {
    geolocation_t* geolocation = (geolocation_t*)(*ctx);
    if (!geolocation) return -1;

    if (location_name) {
        geolocation->location_name = strdup(location_name);
    } else return -1;

    if (location_count && location_count > 0 && location_count <= 10) {
        geolocation->location_count = location_count;
    } else {
        geolocation->location_count = 5;
    }

    if (country_code) {
        geolocation->country_code = strdup(country_code);
    }

    geolocation->locations = malloc(sizeof(location_t) * geolocation->location_count);
    if (!geolocation->locations) {
        return -1;
    }

    return 0;
}

int geolocation_init(void** ctx, void** ctx_struct, void (*on_done)(void* context)) {
    geolocation_t* geolocation = (geolocation_t*)malloc(sizeof(geolocation_t));
    if (!geolocation) { return -1; }

    if (create_folder(GEOLOCATIONS_CACHE_DIR) < 0) {
        printf("Geolocation: Failed to create cache folder\n");
        free(geolocation);
        return -1;
    }

    memset(geolocation, 0, sizeof(geolocation_t));
    geolocation->ctx = ctx;
    geolocation->on_done = on_done;

    geolocation->curl_client = (curl_client_t*)malloc(sizeof(curl_client_t));
    memset(geolocation->curl_client, 0, sizeof(curl_client_t));

    geolocation->location_name = NULL;
    geolocation->location_count = 0;
    geolocation->country_code = NULL;

    geolocation->state = GeoLocation_State_Init;
    *ctx_struct = (void*)geolocation;

    return 0;
}

int geolocation_work(void** ctx) {
    geolocation_t* geolocation = (geolocation_t*)(*ctx);
    if (!geolocation) { return -1; }

    switch (geolocation->state) {
        case GeoLocation_State_Init: {
            printf("GeoLocation: Initialized\n");
            geolocation->state = GeoLocation_State_SearchForCandidates;
            break;
        }
        case GeoLocation_State_SearchForCandidates: {
            printf("GeoLocation: Searching for Candidates\n");
            look_for_candidates(geolocation);
            geolocation->state = GeoLocation_State_FetchFromAPI_Init;
            break;
        }
        case GeoLocation_State_FetchFromAPI_Init: {
            if (curl_client_init(&geolocation->curl_client) != 0) {
                geolocation->state = GeoLocation_State_Done;
                break;
            }
            printf("GeoLocation: Fetching From API\n");
            geolocation->state = GeoLocation_State_FetchFromAPI_Request;
            break;
        }
        case GeoLocation_State_FetchFromAPI_Request: {
            printf("GeoLocation: Making API Request\n");
            char url[4096];
            snprintf(url, sizeof(url), METEO_GEOLOCATION_URL, geolocation->location_name, geolocation->location_count);
            
            // Append country code if provided
            if (geolocation->country_code) {
                char country_param[64];
                snprintf(country_param, sizeof(country_param), "&country=%s", geolocation->country_code);
                strcat(url, country_param);
            }

            if (curl_client_make_request(&geolocation->curl_client, url) != 0) {
                geolocation->state = GeoLocation_State_Done;
                break;
            }
            geolocation->state = GeoLocation_State_FetchFromAPI_Poll;
            break;
        }
        case GeoLocation_State_FetchFromAPI_Poll: {
            printf("GeoLocation: Polling API Response\n");
            if (curl_client_poll(&geolocation->curl_client) != 0) {
                geolocation->state = GeoLocation_State_Done;
                break;
            }
            if (geolocation->curl_client->still_running) {
                break;
            } else {
                geolocation->state = GeoLocation_State_FetchFromAPI_Read;
            }
            break;
        }
        case GeoLocation_State_FetchFromAPI_Read: {
            printf("GeoLocation: Reading API Response\n");
            if (curl_client_read_response(&geolocation->curl_client, &geolocation->buffer)) {
                geolocation->state = GeoLocation_State_Done;
                break;
            }
            // curl_client_cleanup(&geolocation->curl_client);
            geolocation->state = GeoLocation_State_ProcessResponse;
            break;
        }
        case GeoLocation_State_ProcessResponse: {
            char* client_response = NULL;
            if (process_openmeteo_geo_response(geolocation->buffer, &client_response) != 0) {
                geolocation->state = GeoLocation_State_Done;
                printf("GeoLocation: Processing Response Failed\n");
            } else {
                free(geolocation->buffer);
                geolocation->buffer = client_response;
                geolocation->state = GeoLocation_State_SaveToDisk;
                printf("GeoLocation: Processing Response Succeeded\n");
            }
            break;
        }
        case GeoLocation_State_SaveToDisk: {
            printf("GeoLocation: Saving locations to disk\n");
            // if (geolocation->locations != NULL) {
            //     for (int i = 0; i < geolocation->location_count; i++) {
            //         save_location_to_disk(&geolocation->locations[i]);
            //     }
            // }
            geolocation->state = GeoLocation_State_Done;
            break;
        }
        case GeoLocation_State_Done: {
            printf("GeoLocation: Done\n");
            geolocation->on_done(geolocation->ctx);
            break;
        }
    }

    return 0;
}

int geolocation_get_buffer(void** ctx, char** buffer) {
    geolocation_t* geolocation = (geolocation_t*)(*ctx);
    if (!geolocation) { return -1; }
    *buffer = geolocation->buffer;
    return 0;
}

int geolocation_dispose(void** ctx) {
    geolocation_t* geolocation = (geolocation_t*)(*ctx);
    if (!geolocation) { return -1; }

    curl_client_cleanup(&geolocation->curl_client);
    free(geolocation->curl_client);
    geolocation->curl_client = NULL;

    free(geolocation->buffer);
    free(geolocation->location_name);
    free(geolocation->country_code);

    free(geolocation);
    *ctx = NULL;

    return 0;
}

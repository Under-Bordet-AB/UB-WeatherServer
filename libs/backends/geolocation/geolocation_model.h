#include <stdlib.h>

typedef struct location_t {
    int id;
    char* name;
    double longitude;
    double latitude;
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
} geolocation_t;
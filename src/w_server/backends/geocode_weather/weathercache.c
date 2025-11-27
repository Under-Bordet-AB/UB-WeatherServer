#define _GNU_SOURCE

#include "weathercache.h"
#include "../../utils/ui.h"
#include "../../utils/utils.h"
#include "../geocode_weather/geocache.h" // for normalize helper
#include <dirent.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

// Helper: round coordinate to 4 decimals and format
static void fmt_coord4(double v, char* out, size_t out_size) {
    double r = round(v * 10000.0) / 10000.0;
    snprintf(out, out_size, "%.4f", r);
}

// Build filepath: cache/weather/<normalized>-LAT-LON.json
static int build_path_coords(const char* name, double lat, double lon, char* out, size_t out_size) {
    if (!name || !out)
        return -1;
    char lat_s[32];
    char lon_s[32];
    fmt_coord4(lat, lat_s, sizeof(lat_s));
    fmt_coord4(lon, lon_s, sizeof(lon_s));
    int n = snprintf(out, out_size, "%s/%s-%s-%s.json", WEATHERCACHE_DIR, name, lat_s, lon_s);
    return (n >= 0 && (size_t)n < out_size) ? 0 : -1;
}

// Determine if a cached file is current relative to Open-Meteo release schedule (00,15,30,45)
// File is current if its mtime >= last_release_time (rounded down to nearest 15 minutes)
static int is_cache_current(const char* path) {
    struct stat st;
    if (stat(path, &st) != 0)
        return 0;
    time_t now = time(NULL);
    struct tm tm;
    /* Use UTC for cache release boundaries to avoid timezone skew */
    gmtime_r(&now, &tm);
    int minute = tm.tm_min;
    int quarter = (minute / 15) * 15;
    tm.tm_min = quarter;
    tm.tm_sec = 0;
    /* Convert UTC tm to time_t. Use timegm so we don't interpret tm as localtime. */
    time_t last_release = timegm(&tm);
    return st.st_mtime >= last_release;
}

int weathercache_init(void) {
    return create_folder(WEATHERCACHE_DIR);
}

int weathercache_get_by_coords(const char* name, double latitude, double longitude, char** out_body) {
    if (!name || !out_body)
        return -1;
    char path[512];
    if (build_path_coords(name, latitude, longitude, path, sizeof(path)) != 0)
        return -1;
    if (!is_cache_current(path))
        return -1;
    size_t size = 0;
    char* buf = load_image(path, &size);
    if (!buf)
        return -1;
    *out_body = buf;
    return 0;
}

int weathercache_set_by_coords(const char* name, double latitude, double longitude, const char* body) {
    if (!name || !body)
        return -1;
    char path[512];
    char tmp[512];
    if (build_path_coords(name, latitude, longitude, path, sizeof(path)) != 0)
        return -1;
    // tmp path: cache/weather/.<name>-lat-lon.json.tmp
    // We don't want the tmp name to collide; instead create unique tmp per-target
    char tmp2[512];
    if (build_path_coords(name, latitude, longitude, tmp2, sizeof(tmp2)) != 0)
        return -1;
    // replace trailing .json with .tmp
    size_t len = strlen(tmp2);
    if (len > 5 && strcmp(tmp2 + len - 5, ".json") == 0) {
        snprintf(tmp, sizeof(tmp), "%.*s.tmp", (int)(len - 5), tmp2);
    } else {
        snprintf(tmp, sizeof(tmp), "%s.tmp", tmp2);
    }

    // Ensure dir exists
    if (create_folder(WEATHERCACHE_DIR) != 0 && errno != EEXIST) {
        // continue
    }

    FILE* f = fopen(tmp, "wb");
    if (!f)
        return -1;
    size_t written = fwrite(body, 1, strlen(body), f);
    fclose(f);
    if (written != strlen(body)) {
        unlink(tmp);
        return -1;
    }
    if (rename(tmp, path) != 0) {
        unlink(tmp);
        return -1;
    }
    return 0;
}

int weathercache_remove_by_coords(const char* name, double latitude, double longitude) {
    if (!name)
        return -1;
    char path[512];
    if (build_path_coords(name, latitude, longitude, path, sizeof(path)) != 0)
        return -1;
    if (unlink(path) != 0)
        return -1;
    return 0;
}

int weathercache_cleanup(time_t max_age_seconds) {
    if (max_age_seconds <= 0)
        return 0;
    DIR* d = opendir(WEATHERCACHE_DIR);
    if (!d)
        return -1;
    struct dirent* de;
    time_t now = time(NULL);
    while ((de = readdir(d)) != NULL) {
        if (de->d_type != DT_REG)
            continue;
        size_t len = strlen(de->d_name);
        if (len < 6)
            continue;
        if (strcmp(de->d_name + len - 5, ".json") != 0)
            continue;
        char path[512];
        int r = snprintf(path, sizeof(path), "%s/%s", WEATHERCACHE_DIR, de->d_name);
        if (r < 0 || (size_t)r >= sizeof(path))
            continue;
        struct stat st;
        if (stat(path, &st) != 0)
            continue;
        if ((now - st.st_mtime) > max_age_seconds)
            unlink(path);
    }
    closedir(d);
    return 0;
}

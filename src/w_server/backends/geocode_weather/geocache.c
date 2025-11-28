#define _GNU_SOURCE

#include "geocache.h"
#include "utils.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define INITIAL_CAPACITY 256

struct geocache_entry {
    char key[128];  // normalized key
    char name[128]; // resolved/canonical name
    double latitude;
    double longitude;
};

struct geocache {
    struct geocache_entry* entries;
    size_t count;
    size_t capacity;
    int dirty;
};

// Normalize name to lowercase, trimmed
char* geocache_normalize_name(const char* city_name, char* out_buf, size_t out_size) {
    if (!city_name || !out_buf || out_size == 0)
        return NULL;

    // Skip leading whitespace
    while (*city_name && isspace((unsigned char)*city_name))
        city_name++;

    size_t j = 0;
    for (size_t i = 0; city_name[i] && j < out_size - 1; i++) {
        unsigned char c = (unsigned char)city_name[i];
        if (c < 128) {
            out_buf[j++] = (char)tolower(c);
        } else {
            out_buf[j++] = city_name[i];
        }
    }

    /* Null-terminate the buffer before calling helpers that expect
     * a C-string. This avoids reading uninitialised bytes reported by
     * Valgrind when the allocated buffer region contains leftover data.
     */
    out_buf[j] = '\0';

    /* Also map a few common Swedish uppercase UTF-8 sequences to lowercase
     * (Å, Ä, Ö) so lookups are case-insensitive for these letters. This
     * delegates to the general helper which handles ASCII and these
     * two-byte UTF-8 sequences.
     */
    utils_to_lowercase(out_buf);

    /* Trim trailing whitespace after case-folding */
    j = strlen(out_buf);
    while (j > 0 && isspace((unsigned char)out_buf[j - 1]))
        j--;

    out_buf[j] = '\0';
    return out_buf;
}

// Round to 4 decimal places
static double round4(double v) {
    return round(v * 10000.0) / 10000.0;
}

static int ensure_capacity(geocache_t* cache) {
    if (!cache)
        return -1;
    if (cache->count < cache->capacity)
        return 0;
    size_t newcap = cache->capacity ? cache->capacity * 2 : INITIAL_CAPACITY;
    struct geocache_entry* new_entries = realloc(cache->entries, newcap * sizeof(*new_entries));
    if (!new_entries)
        return -1;
    cache->entries = new_entries;
    cache->capacity = newcap;
    return 0;
}

geocache_t* geocache_load(void) {
    geocache_t* cache = calloc(1, sizeof(geocache_t));
    if (!cache)
        return NULL;

    cache->entries = NULL;
    cache->count = 0;
    cache->capacity = 0;
    cache->dirty = 0;

    // Ensure directory exists
    create_folder(GEOCACHE_DIR);

    FILE* f = fopen(GEOCACHE_FILE, "r");
    if (!f) {
        // No file yet; return empty cache
        return cache;
    }

    char line[512];
    while (fgets(line, sizeof(line), f)) {
        // Trim newline
        char* nl = strchr(line, '\n');
        if (nl)
            *nl = '\0';

        // Skip empty lines
        char* p = line;
        while (*p && isspace((unsigned char)*p))
            p++;
        if (*p == '\0')
            continue;

        // Parse: name,lat,lon
        char* comma1 = strchr(line, ',');
        if (!comma1)
            continue;
        *comma1 = '\0';
        char* name = line;
        char* latstr = comma1 + 1;
        char* comma2 = strchr(latstr, ',');
        if (!comma2)
            continue;
        *comma2 = '\0';
        char* lonstr = comma2 + 1;

        double lat = atof(latstr);
        double lon = atof(lonstr);

        // Insert into cache
        if (ensure_capacity(cache) != 0) {
            fclose(f);
            geocache_destroy(cache);
            return NULL;
        }
        struct geocache_entry* e = &cache->entries[cache->count++];
        // store normalized key
        geocache_normalize_name(name, e->key, sizeof(e->key));
        // store display name as-is (trimmed)
        strncpy(e->name, name, sizeof(e->name) - 1);
        e->name[sizeof(e->name) - 1] = '\0';
        e->latitude = round4(lat);
        e->longitude = round4(lon);
    }

    fclose(f);
    cache->dirty = 0;
    return cache;
}

int geocache_save(geocache_t* cache) {
    if (!cache)
        return -1;

    create_folder(GEOCACHE_DIR);

    FILE* f = fopen(GEOCACHE_FILE, "w");
    if (!f)
        return -1;

    /* Write only the first occurrence of each normalized key to disk.
     * This avoids duplicate lines for the same logical location when the
     * in-memory cache contains more than one entry with the same key
     * (which can happen if the file was edited externally or loaded from
     * older dumps). We keep the first seen entry and skip later duplicates.
     */
    for (size_t i = 0; i < cache->count; i++) {
        int seen = 0;
        for (size_t j = 0; j < i; j++) {
            if (strcmp(cache->entries[j].key, cache->entries[i].key) == 0) {
                seen = 1;
                break;
            }
        }
        if (seen)
            continue;
        // Write: name,latitude,longitude
        // Use the stored display name (may contain UTF-8) without quoting
        fprintf(f, "%s,%.4f,%.4f\n", cache->entries[i].name, cache->entries[i].latitude, cache->entries[i].longitude);
    }

    fclose(f);
    cache->dirty = 0;
    return 0;
}
// returns 0 on hit
int geocache_lookup(geocache_t* cache, const char* city_name, geocache_entry_t* entry) {
    if (!cache || !city_name || !entry)
        return -1;

    /* Case-insensitive lookup: normalize the provided city_name using the
     * same normalization function used when storing keys (geocache_normalize_name)
     * and compare that normalized key against the stored `key` field. This
     * makes lookups insensitive to ASCII case and common Swedish letters.
     */
    char normalized[128];
    if (!geocache_normalize_name(city_name, normalized, sizeof(normalized)))
        return -1;

    for (size_t i = 0; i < cache->count; i++) {
        if (strcmp(cache->entries[i].key, normalized) == 0) {
            entry->latitude = cache->entries[i].latitude;
            entry->longitude = cache->entries[i].longitude;
            strncpy(entry->name, cache->entries[i].name, sizeof(entry->name) - 1);
            entry->name[sizeof(entry->name) - 1] = '\0';
            return 0;
        }
    }

    return -1; // miss
}

int geocache_insert(geocache_t* cache,
                    const char* city_name,
                    double latitude,
                    double longitude,
                    const char* resolved_name) {
    if (!cache || !city_name)
        return -1;

    // Reject invalid coordinates (e.g., 0.0,0.0 or near-zero indicates geocoding failure)
    if (fabs(latitude) < 0.0001 && fabs(longitude) < 0.0001) {
        return -1; // Don't insert invalid coords
    }

    char normalized[128];
    if (!geocache_normalize_name(city_name, normalized, sizeof(normalized)))
        return -1;

    // Update if exists
    for (size_t i = 0; i < cache->count; i++) {
        if (strcmp(cache->entries[i].key, normalized) == 0) {
            cache->entries[i].latitude = latitude;
            cache->entries[i].longitude = longitude;
            if (resolved_name) {
                strncpy(cache->entries[i].name, resolved_name, sizeof(cache->entries[i].name) - 1);
                cache->entries[i].name[sizeof(cache->entries[i].name) - 1] = '\0';
            }
            cache->dirty = 1;
            return 0;
        }
    }

    if (ensure_capacity(cache) != 0)
        return -1;

    struct geocache_entry* e = &cache->entries[cache->count++];
    strncpy(e->key, normalized, sizeof(e->key) - 1);
    e->key[sizeof(e->key) - 1] = '\0';
    if (resolved_name) {
        strncpy(e->name, resolved_name, sizeof(e->name) - 1);
        e->name[sizeof(e->name) - 1] = '\0';
    } else {
        strncpy(e->name, city_name, sizeof(e->name) - 1);
        e->name[sizeof(e->name) - 1] = '\0';
    }
    e->latitude = round4(latitude);
    e->longitude = round4(longitude);
    cache->dirty = 1;
    return 0;
}

size_t geocache_count(geocache_t* cache) {
    if (!cache)
        return 0;
    return cache->count;
}

int geocache_is_dirty(geocache_t* cache) {
    if (!cache)
        return 0;
    return cache->dirty;
}

void geocache_destroy(geocache_t* cache) {
    if (!cache)
        return;
    free(cache->entries);
    free(cache);
}

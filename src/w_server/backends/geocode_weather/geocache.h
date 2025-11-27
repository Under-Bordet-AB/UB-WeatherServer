#ifndef GEOCACHE_H
#define GEOCACHE_H

#include <stddef.h>

/*
    Geocache Module
    ---------------
    In-memory cache of city name -> coordinates mappings.

    Usage:
    1. Call geocache_load() at server startup (loads from disk if exists)
    2. Call geocache_lookup() before making geocode API calls
    3. Call geocache_insert() after successful API lookups
    4. Call geocache_save() periodically or on shutdown to persist
    5. Call geocache_destroy() on shutdown to free memory

    The cache is stored as a CSV file: `cache/cache.csv` with one entry per line.
    Format (CSV):
        name,latitude,longitude
    Example:
        Stockholm,59.32938,18.06871
        Göteborg,57.70887,11.97456

    Keys are normalized (lowercase, trimmed) in-memory when looked up.
*/
#define GEOCACHE_DIR "cache"
#define GEOCACHE_FILE "cache/location_cordinates.csv"

// Opaque handle to the geocache
typedef struct geocache geocache_t;

// Entry returned by lookup
typedef struct {
    double latitude;
    double longitude;
    char name[128]; // Resolved/canonical city name from API
} geocache_entry_t;

// Create and load geocache from disk (or create empty if file doesn't exist)
// Returns NULL on allocation failure
geocache_t* geocache_load(void);

// Save geocache to disk
// Returns 0 on success, -1 on error
int geocache_save(geocache_t* cache);

// Lookup a city in the cache
// Returns 0 and fills entry on hit, -1 on miss
int geocache_lookup(geocache_t* cache, const char* city_name, geocache_entry_t* entry);

// Insert/update a city in the cache
// Returns 0 on success, -1 on error
int geocache_insert(geocache_t* cache,
                    const char* city_name,
                    double latitude,
                    double longitude,
                    const char* resolved_name);

// Get number of entries in cache
size_t geocache_count(geocache_t* cache);

// Mark cache as dirty (needs save)
int geocache_is_dirty(geocache_t* cache);

// Free all memory used by geocache
void geocache_destroy(geocache_t* cache);

// Utility: normalize city name (lowercase, trim whitespace)
// Writes to out_buf, returns out_buf or NULL on error
char* geocache_normalize_name(const char* city_name, char* out_buf, size_t out_size);

#endif // GEOCACHE_H

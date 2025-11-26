#pragma once

#include <stddef.h>
#include <time.h>

// Simple per-location weather cache (one JSON file per location)

// Ensure weather cache directory exists and initialize any state (currently a noop)
int weathercache_init(void);

// Get cached weather JSON for a location identified by name and coordinates.
// The cache file is named: `cache/weather/<normalized>-LAT-LON.json` where LAT/LON
// are rounded to 4 decimal places. Returns 0 on hit and sets *out_body (malloc'd).
// On miss/expired/error returns -1.
int weathercache_get_by_coords(const char* name, double latitude, double longitude, char** out_body);

// Save weather JSON body for given name+coords (atomic write). Returns 0 on success.
int weathercache_set_by_coords(const char* name, double latitude, double longitude, const char* body);

// Remove cached file for given name+coords. Returns 0 on success or -1 if not found.
int weathercache_remove_by_coords(const char* name, double latitude, double longitude);

// Cleanup/evict files older than `max_age_seconds` from the cache directory
int weathercache_cleanup(time_t max_age_seconds);

// Path constants
#define WEATHERCACHE_DIR "cache/weather"

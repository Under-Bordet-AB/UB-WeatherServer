# Improved Route Handling for WeatherServer

## Overview

The current route matching in `w_client.c` uses a long chain of hardcoded `if` statements, which is functional but not professional or scalable. This document proposes a refactor to use a route table with handler functions for better maintainability, readability, and extensibility.

## Issues with Current Approach

- **Repetition**: Every route checks the method and URL manually.
- **Hardcoded**: Adding a new route requires editing core logic.
- **Inconsistent**: Weather route uses fragile `sscanf` parsing; others use exact matches.
- **No Separation of Concerns**: Route logic is mixed with response building.
- **Scalability**: `if` chain becomes unwieldy with more routes.

## Proposed Solution: Route Table with Handlers

Introduce a `route_t` struct and handler functions to encapsulate route logic. This separates routing from processing and makes adding routes trivial.

### Code Changes

#### 1. Define Route Structure and Handlers

Add this near the top of `src/w_server/w_client.c` (after includes):

```c
typedef struct {
    const char* method;
    const char* path;
    void (*handler)(w_client* client, http_request* req);
} route_t;

// Handler functions (move response logic here for cleanliness)
void handle_root(w_client* client, http_request* req) {
    client->response_body = http_msg_200_ok_text("Hello from weather server!");
    client->response_len = http_msg_get_total_size(client->response_body);
    client->response_sent = 0;
    ui_print_response_details(client, 200, "OK", client->response_len);
    client->state = W_CLIENT_SENDING;
}

void handle_health(w_client* client, http_request* req) {
    client->response_body = http_msg_200_ok_text("OK");
    client->response_len = http_msg_get_total_size(client->response_body);
    client->response_sent = 0;
    ui_print_response_details(client, 200, "OK", client->response_len);
    client->state = W_CLIENT_SENDING;
}

void handle_index(w_client* client, http_request* req) {
    // Existing index.html logic (unchanged, just moved to a function)
    FILE* f = fopen("www/index.html", "rb");
    if (f) {
        if (fseek(f, 0, SEEK_END) == 0) {
            long sz = ftell(f);
            if (sz >= 0) {
                if (fseek(f, 0, SEEK_SET) == 0) {
                    char* buf = malloc((size_t)sz + 1);
                    if (buf) {
                        size_t r = fread(buf, 1, (size_t)sz, f);
                        buf[r] = '\0';
                        if (r == (size_t)sz) {
                            // Build a proper text/html response
                            client->response_body = http_msg_build_response(200, "OK", "text/html", buf, NULL);
                        }
                        free(buf);
                    }
                }
            }
        }
        fclose(f);
    }

    if (!client->response_body) {
        // Fallback embedded HTML when file missing or failed to read
        const char* html = "<html><head><title>WeatherServer</title></head><body><h1>WeatherServer</h1><p>Welcome. <br> No index.html found.</p></body></html>";
        client->response_body = http_msg_build_response(200, "OK", "text/html", html, NULL);
    }

    if (client->response_body) {
        client->response_len = http_msg_get_total_size(client->response_body);
        client->response_sent = 0;
        ui_print_response_details(client, 200, "OK", client->response_len);
        client->state = W_CLIENT_SENDING;
    } else {
        client->error_code = W_CLIENT_ERROR_INTERNAL;
        client->state = W_CLIENT_SENDING;
    }
}

void handle_surprise(w_client* client, http_request* req) {
    // Existing surprise logic (unchanged, just moved to a function)
    FILE* fptr = fopen("www/bonzi.png", "rb"); // "rb" - read binary
    if (!fptr) {
        client->error_code = W_CLIENT_ERROR_ROUTE_SURPRISE;
        client->state = W_CLIENT_DONE;
        return;
    }

    // Calculate file size
    fseek(fptr, 0, SEEK_END);
    long file_size_raw = ftell(fptr);
    if (file_size_raw < 0) {
        fclose(fptr);
        client->error_code = W_CLIENT_ERROR_ROUTE_SURPRISE;
        client->state = W_CLIENT_DONE;
        return;
    }
    size_t file_size = (size_t)file_size_raw;
    fseek(fptr, 0, SEEK_SET);

    uint8_t* buffer = (uint8_t*)malloc(sizeof(uint8_t) * file_size);
    if (!buffer) {
        // Failed to allocated memory
        fclose(fptr);
        client->error_code = W_CLIENT_ERROR_ROUTE_SURPRISE;
        client->state = W_CLIENT_DONE;
        return;
    }

    size_t bytes_read = fread(buffer, 1, file_size, fptr);
    if (bytes_read != file_size) {
        // Failed to read file
        free(buffer);
        fclose(fptr);
        client->error_code = W_CLIENT_ERROR_ROUTE_SURPRISE;
        client->state = W_CLIENT_DONE;
        return;
    }

    // Success!
    fclose(fptr);

    // Build response
    client->response_body = http_msg_200_ok_binary("image/png", buffer, file_size);
    client->response_len = http_msg_get_total_size(client->response_body);

    client->state = W_CLIENT_SENDING;
}

// Route table (add new routes here easily)
route_t routes[] = {
    {"GET", "/", handle_root},
    {"GET", "/health", handle_health},
    {"GET", "/index.html", handle_index},
    {"GET", "/surprise", handle_surprise},
    // Add more static routes here, e.g., {"GET", "/status", handle_status}
};
const size_t num_routes = sizeof(routes) / sizeof(routes[0]);
```

#### 2. Refactor the Processing Logic

Replace the `if` chain in the `W_CLIENT_PROCESSING` case with a loop over the route table. Keep the weather route separate (since it's async), but improve its URL matching to reject malformed URLs.

Update the `W_CLIENT_PROCESSING` case:

```c
case W_CLIENT_PROCESSING: {
    // Route request and dispatch to appropriate backend
    http_request* req = (http_request*)client->parsed_request;
    ui_print_processing_request(client);

    // Parse city from URL query parameter (e.g., "/weather?location=stockholm")
    int city_in_url = 0;
    if (sscanf(req->url, "%*[^=]=%s", client->requested_city) == 1) {
        city_in_url = 1;
        /* Decode %-encoded sequences (e.g. %C3%85) into raw bytes so
         * utils_to_lowercase() can lower-case UTF-8 sequences like
         * Å/Ä/Ö correctly. Then lower-case the string to produce a
         * canonical key used throughout the system. */
        utils_decode_swedish_chars(client->requested_city);
        /* Lowercase only Swedish multibyte letters, keep ASCII case intact
         * so we preserve exact-match semantics for ASCII variants. */
        utils_lowercase_swedish_letters(client->requested_city);
    }

    // Check static routes first
    int route_matched = 0;
    for (size_t i = 0; i < num_routes; i++) {
        if (strcmp(req->method, routes[i].method) == 0 && strcmp(req->url, routes[i].path) == 0) {
            routes[i].handler(client, req);
            route_matched = 1;
            break;
        }
    }
    if (route_matched) break;

    // WEATHER ROUTE (async, kept separate)
    if (req->method == REQUEST_METHOD_GET && strncmp(req->url, "/weather?", 9) == 0 && city_in_url) {
        // ... existing weather logic, but with improved URL check ...
        // Check geocache first - if we have cached coordinates, pass them to the task
        geocache_entry_t cached_entry;
        int cache_hit = 0;
        if (client->server->geocache) {
            if (geocache_lookup(client->server->geocache, client->requested_city, &cached_entry) == 0) {
                cache_hit = 1;
                ui_print_backend_state(client, "GeocodeWeather", "geocache hit");
            }
        }

        // Create geocode_weather task to fetch coordinates then weather
        mj_task* gw_task;
        if (cache_hit) {
            // Pass cached coordinates - task will skip geocode API call
            gw_task = geocode_weather_task_create_with_coords(
                client, client->server->geocache, cached_entry.latitude, cached_entry.longitude, cached_entry.name);
        } else {
            // No cache hit - task will do full geocode + weather lookup
            gw_task = geocode_weather_task_create(client, client->server->geocache);
        }

        if (!gw_task) {
            client->error_code = W_CLIENT_ERROR_INTERNAL;
            client->state = W_CLIENT_SENDING;
            break;
        }

        // Add task to scheduler - it will set client->response_body when done
        if (mj_scheduler_task_add(scheduler, gw_task) < 0) {
            free(gw_task->ctx);
            free(gw_task);
            client->error_code = W_CLIENT_ERROR_INTERNAL;
            client->state = W_CLIENT_SENDING;
            break;
        }

        // Wait for task to complete
        client->state = W_CLIENT_WAITING_TASK;
        break;
    }

    // No valid route matched
    client->error_code = W_CLIENT_ERROR_MALFORMED_REQUEST;
    client->state = W_CLIENT_SENDING;
    break;
}
```

## Benefits

- **Professional**: Clean separation of routing and handling.
- **Maintainable**: Easy to add routes by extending the `routes` array.
- **Robust**: Weather route rejects invalid URLs (e.g., `/?weather?location=...`).
- **Extensible**: Supports dynamic routes later if needed.
- **No Breaking Changes**: Existing behavior is preserved.

## Testing

Rebuild with `make` and test with `test_endpoints.sh`. Valid requests should work; invalid ones (like `/?weather?location=Stockholm`) should return 400 Bad Request.

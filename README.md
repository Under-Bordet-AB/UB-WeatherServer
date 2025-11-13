# 🌦️ UB-WeatherServer

Simple HTTP weather API server using cooperative multitasking.

## 📚 Documentation

- **[Architecture Analysis](ARCHITECTURE_ANALYSIS.md)** — comprehensive design pattern analysis, bugs, and improvement roadmap
- **[Quick Task Checklist](QUICK_TASK_CHECKLIST.md)** — actionable todo list with priorities
- **[V2 Scaffold README](V2/README_V2.md)** — next-generation architecture proposal

## 🚀 Quick Start

```bash
# Build (release mode)
make

# Build (debug mode)
make MODE=debug

# Run server
./server

# Test endpoints
curl http://localhost:8080/health
curl http://localhost:8080/cities
curl http://localhost:8080/weather/Stockholm
```

## 🔧 Development

```bash
# Format all code
git ls-files '*.c' '*.h' | xargs -r clang-format -i

# Check for memory leaks
make MODE=debug
valgrind --leak-check=full ./server

# Clean build artifacts
make clean
```

## 📋 Current Status

**Critical issues identified (see ARCHITECTURE_ANALYSIS.md):**
- ⚠️ Memory leaks in connection lifecycle
- ⚠️ CPU waste from busy-loop polling
- ⚠️ State machine bugs

**Priority fixes in progress:**
1. Fix memory leaks
2. Add epoll-based event loop
3. Improve error handling

## 🌐 API Documentation
## Retrieve list of available locations.
*   **Method:** `GET`
*   **Path:** `/cities`
*   **Example Request:**
    ```bash
    curl -X GET http://localhost:8080/cities
    ```
*   **Expected Response (JSON):**
    ```json
    {
        "cities": [
            { "name": "Stockholm", "latitude": 59.3293, "longitude": 18.0686 },
            { "name": "Göteborg", "latitude": 57.7089, "longitude": 11.9746 },
            { "name": "Malmö", "latitude": 55.6050, "longitude": 13.0038 }
        ]
    }
    ```
*   **Expected Error Response (JSON):**
    ```json
    {
        "error": {
            "code": 404,
            "message": "Location not found"
        }
    }
    ```

## Retrieve current weather data for a specified location.
*   **Method:** `GET`
*   **Path:** `/weather/{location}`
*   **URL Parameters:**
    *   `location` (string, required): The name of the city or location (e.g., `Stockholm`, `London`, `New%20York`).
*   **Example Request:**
    ```bash
    curl -X GET http://localhost:8080/weather/Stockholm
    ```
*   **Expected Response (JSON):**
    ```json
    {
        "latitude": "59.3293",
        "longitude": "18.0686",
        "location": "Stockholm",
        "temperature": 15.2,
        "unit": "celsius",
        "condition": "Partly Cloudy",
        "humidity": 70,
        "wind_speed": 10.5,
        "wind_direction": "NW"
    }
    ```
*   **Expected Error Response (JSON):**
    ```json
    {
        "error": {
            "code": 400,
            "message": "Bad request: missing or invalid location parameter"
        }
    }
    ```

## Retrieve surprise.
*   **Method:** `GET`
*   **Path:** `/surprise`
*   **Example Request:**
    ```bash
    curl -X GET http://localhost:8080/weather/surprise
    ```
*   **Expected Response (JSON):**
    ```json
    "?"
    ```
    
    *Note: The actual response format may vary slightly once implemented.*

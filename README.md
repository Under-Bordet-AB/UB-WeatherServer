# 🌦️ UB-WeatherServer

Simple HTTP weather API server using cooperative multitasking.

## 🚀 Quick Start

```bash
# Build (release mode)
make

# Build (debug mode)
make MODE=debug

# Run server
./server

# Test endpoints
curl http://localhost:8080/GetCities
curl http://localhost:8080/GetLocation?name=Stockholm&count=5
curl http://localhost:8080/GetWeather?lon=59.16&lat=29.14
curl http://localhost:8080/GetSurprise
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

## 🌐 API Documentation
## Retrieve list of default locations.
*   **Method:** `GET`
*   **Path:** `/GetCities`
*   **Example Request:**
    ```bash
    curl -X GET http://localhost:8080/GetCities
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

## Retrieve a list of locations
*   **Method:** `GET`
*   **Path:** `/GetLocation`
*   **Example Request:**
    ```bash
    curl -X GET http://localhost:8080/GetLocation?name=Stockholm&count=5
    ```
*   **Expected Response(JSON):**
    ```json
    [
        {
            "id": 2711537,
            "name": "Gothenburg",
            "latitude": 57,
            "longitude": 11,
            "elevation": 10,
            "feature_code": "PPLA",
            "country_code": "SE",
            "..."
        },
        {
            "id": 2730934,
            "name": "Göteborgnuten",
            "latitude": 79,
            "longitude": 11,
            "elevation": 190,
            "feature_code": "MT",
            "country_code": "SJ",
            "..."
        }
    ]
    ```

## Retrieve current weather data for a specified location.
*   **Method:** `GET`
*   **Path:** `/GetWeather?lat=59.3293&lon=18.0686`
*   **URL Parameters:**
    *   `lat` (double, required): The latitude of the city or location.
    *   `lon` (double, required): The longitude of the city or location.
*   **Example Request:**
    ```bash
    curl -X GET http://localhost:8080/GetWeather?lat=59.3293&lon=18.0686
    ```
*   **Expected Response (JSON):**
    ```json
    {
        "latitude": "59.3293",
        "longitude": "18.0686",
        "...",
        "time": "2025-12-08T12:45",
        "temperature_2m": 15.2,
        "precipitation": 0.2,
        "wind_speed_10m": 24.1,
    }
    ```

## Retrieve surprise.
*   **Method:** `GET`
*   **Path:** `/surprise`
*   **Example Request:**
    ```bash
    curl -X GET http://localhost:8080/GetSurprise
    ```
*   **Expected Response (JSON):**
    ```json
    "?"
    ```

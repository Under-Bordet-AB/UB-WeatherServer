# 🌦️Documentation
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
            { "name": "Gothenburg", "latitude": 57.7089, "longitude": 11.9746 },
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
*   ```bash
    {
        curl -X GET http://localhost:8080/weather/surprise
    }
    ```
*   **Expected Response (JSON):**
    ```json
    "?"
    ```
    
    *Note: The actual response format may vary slightly once implemented.*

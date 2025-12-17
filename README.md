# UB-WeatherServer
HTTP/HTTPS weather API server using cooperative multitasking.
Schoolproject @ Chas Academy, class SUVX 2025.

## Build rules
```bash
make all          # Builds entire project, debug as default (change MODE ?= for release)
make asan         # Builds with ASAN
```
- If running with real cert: set absolute path to cert in root project folder in global_define.h (CERT_FILE_PATH, PRIVKEY_FILE_PATH)
- If runnnig with real cert: set #define SKIP_TLS_CERT_FOR_DEV 0  // Set to 1 for dev in global_define.h
- TLS_PORT set in global_define (default: 10443)

### Example of compiling and running
```bash
make -j<val>      # or without -j for singel core
./server <port>   # ^C to exit program
```

## Endpoints
| Endpoint | Method | Description |
|----------|--------|-------------|
| `/GetCities` | GET | List available cities (JSON) |
| `/GetLocation` | GET | Geocode location name to coordinates |
| `/GetWeather` | GET | Get weather by latitude/longitude |
| `/GetSurprise` | GET | Get a surprise (binary image) |

### GetCities
```bash
curl http://localhost:8080/GetCities
curl -k -v https://localhost:8080/GetCities
```
Returns JSON with city names and coordinates.

### GetLocation
```bash
curl http://localhost:8080/GetLocation?name=Stockholm&count=5&countryCode=SE
curl -k -v curl https://localhost:8080/GetLocation?name=Stockholm&count=5&countryCode=SE
```
Parameters: `name` (required), `count` (optional), `countryCode` (optional)  
Returns JSON with matching locations.

### GetWeather
```bash
curl http://localhost:8080/GetWeather?lat=59.33&lon=18.07
curl -k -v https://localhost:8080/GetLocation?name=Stockholm&count=5&countryCode=SE
```
Parameters: `lat` (required), `lon` (required)  
Returns JSON with weather data.

### GetSurprise
```bash
curl http://localhost:8080/GetSurprise
curl -k -v https://localhost:8080/GetSurprise
```
Returns binary PNG image.

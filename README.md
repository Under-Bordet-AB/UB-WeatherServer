# UB-WeatherServer

HTTP/HTTPS weather API server using cooperative multitasking.

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
```
Returns JSON with city names and coordinates.

### GetLocation
```bash
curl "http://localhost:8080/GetLocation?name=Stockholm&count=5&countryCode=SE"
```
Parameters: `name` (required), `count` (optional), `countryCode` (optional)  
Returns JSON with matching locations.

### GetWeather
```bash
curl "http://localhost:8080/GetWeather?lat=59.33&lon=18.07"
```
Parameters: `lat` (required), `lon` (required)  
Returns JSON with weather data.

### GetSurprise
```bash
curl http://localhost:8080/GetSurprise > image.png
```
Returns binary PNG image.

## Build & Run
```bash
make              # build (release)
make MODE=debug   # build (debug)
./server          # run
```

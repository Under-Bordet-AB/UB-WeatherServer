# UB-WeatherServer

HTTP weather API server using cooperative multitasking.

## Installation

### Prerequisites
Install required development libraries:

```bash
sudo apt-get update
sudo apt-get install -y build-essential libcurl4-openssl-dev libmbedtls-dev
```

This installs:
- **build-essential** - Compiler and build tools
- **libcurl4-openssl-dev** - HTTP client library
- **libmbedtls-dev** - TLS/SSL encryption library


## Onboarding
- [ToDo.md](ToDo.md) <- Look here for suggestions before contributing.
- [ARCHITECTURE_ANALYSIS.md](ARCHITECTURE_ANALYSIS.md) <- Project walk through.

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
curl http://localhost:8080/GetSurprise
```
Returns binary PNG image.

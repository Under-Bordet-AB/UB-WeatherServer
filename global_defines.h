// Central place for configurable defines.
// Use the pattern: `#define NAME value // From <original file>`

// Parser defaults (G_ prefixed). These set centralized parser values that
// `libs/HTTPParser.h` will map to. Use the `G_` prefix to make it explicit
// that these are global configuration values.
// If you prefer the parser's built-in defaults, remove or change these.

// G_ prefixed HTTP parser defaults (from libs/HTTPParser.h)
#define G_HTTP_VERSION "HTTP/1.1" // From libs/HTTPParser.h
#define G_CLOSE_CONNECTIONS 1 // From libs/HTTPParser.h
#define G_MAX_URL_LEN 256 // From libs/HTTPParser.h
#define G_STRICT_VALIDATION 1 // From libs/HTTPParser.h
#define G_CORS_ALLOWED_ORIGIN "*" // From libs/HTTPParser.h
#define G_CORS_ALLOWED_METHODS "GET, OPTIONS" // From libs/HTTPParser.h
#define G_CORS_ALLOWED_HEADERS "" // From libs/HTTPParser.h

#ifndef GLOBAL_DEFINES_H
#define GLOBAL_DEFINES_H

// TCPServer limits
#define TCPServer_MAX_CLIENTS 10 // From libs/TCPServer.h
#define TCPServer_MAX_CONNECTIONS_PER_WINDOW 10 // From libs/TCPServer.h
#define TCPServer_MAX_CONNECTIONS_WINDOW_SECONDS 10 // From libs/TCPServer.h

// HTTP server connection buffers/timeouts
#define HTTPServerConnection_READBUFFER_SIZE 4096 // From libs/HTTPServer/HTTPServerConnection.h
#define HTTPServerConnection_WRITEBUFFER_SIZE 4096 // From libs/HTTPServer/HTTPServerConnection.h
#define HTTPServerConnection_HTTPSERVER_TIMEOUT_MS 1000 // From libs/HTTPServer/HTTPServerConnection.h

// smw task limits
#define smw_max_tasks 16 // From libs/smw.h

// Surprise backend files
#define Surprise_IMAGE_NAME "surprise.png" // From libs/backends/surprise/surprise.c
#define Surprise_FOLDER "./resources/surprise/" // From libs/backends/surprise/surprise.c

// Cache directories used by backends
#define Cities_CACHE_DIR "cache/cities" // From libs/backends/cities/cities.c
#define Weather_CACHE_DIR "cache/weather" // From libs/backends/weather/weather.c

// Defaults used by WeatherServerInstance for geolocation searches
#define WeatherServerInstance_DEFAULT_LOCATION_COUNT 5 // From WeatherServerInstance.c

// Server listen configuration
// Port used by the HTTP server (originally hardcoded in libs/HTTPServer/HTTPServer.c)
//#define WeatherServer_LISTEN_PORT "10480" // From libs/HTTPServer/HTTPServer.c
#define LISTEN_PORT_MAX_SIZE 16
#define LISTEN_PORT_RANGE 65535
#define TLS_PORT "10443"
#define CERT_FILE_PATH "/home/stockholm1/UB-WeatherServer/cert/fullchain.pem"
#define PRIVKEY_FILE_PATH "/home/stockholm1/UB-WeatherServer/cert/privkey.pem"

// External API timeouts (seconds)
// General curl timeouts used by `libs/utilities/curl_client.c` and backend clients
#define CURL_CONNECT_TIMEOUT_SEC 3 // From libs/utilities/curl_client.c (choose sensible default)
#define CURL_REQUEST_TIMEOUT_SEC 10 // From libs/utilities/curl_client.c (overall request timeout)

// Backend-specific API timeouts (override per-backend as needed)
#define Geolocation_API_TIMEOUT_SEC CURL_REQUEST_TIMEOUT_SEC // From libs/backends/geolocation/geolocation.c
#define Weather_API_TIMEOUT_SEC CURL_REQUEST_TIMEOUT_SEC // From libs/backends/weather/weather.c

// Curl response buffer cap to avoid unbounded allocations (bytes)
// Enforced in `libs/utilities/curl_client.c` write callback
#define CURL_CLIENT_MAX_RESPONSE_SIZE (1024 * 1024 * 5) // 5MB cap, from libs/utilities/curl_client.c

#endif // GLOBAL_DEFINES_H

# UB-WeatherServer

HTTP server that resolves city names to coordinates and serves weather data via Open‑Meteo.

## Usage

### Building

- `make` or `make debug`: Build debug binary with ASAN (default)
- `make release`: Optimized release build
- `make profile`: Build for profiling (DWARF4, compatible with Valgrind and perf)

### Running

```bash
./server [port] [bind_address]
```

- Defaults: port 10480, bind 127.0.0.1 (localhost only)
- **To listen on the internet**: Use `0.0.0.0` to bind to all network interfaces (makes the server accessible from any IP), or specify a specific IP address (e.g., your server's public or private IP).
- Examples:
  - Local only: `./server` or `./server 10480 127.0.0.1`
  - All interfaces (internet accessible): `./server 10480 0.0.0.0`
  - Specific IP: `./server 10480 192.168.1.100` (replace with your actual IP)
- **Security note**: Binding to `0.0.0.0` or a public IP exposes the server to the internet. Ensure your firewall allows only necessary traffic, and consider using HTTPS or a reverse proxy in production.

### Available endpoints

- `/weather?location=<x>` - Weather lookup for `<x>`
- `/index.html` - Server monitoring webpage
- `/surprise` - Surprise endpoint
- `/health` - Returns `"OK"` if the server is alive
- `/` - Hello message

### Testing with curl

```bash
# Root
curl -i http://localhost:10480/

# Index page
curl -i http://localhost:10480/index.html

# Health check
curl -i http://localhost:10480/health

# Weather (case insensitive & Swedish ÅÄÖåäö works)
curl -i "http://localhost:10480/weather?location=Åre"

# Surprise image
curl -i http://localhost:10480/surprise --output surprise.png
```

### Makefile Targets

- `make run`: Build and run debug binary
- `make run-release`: Build and run release binary
- `make stress`: Build stress test binary (`./stress -count eternal -burst -realistic -nr 1022`)
- `make perf-record`: Record perf profile (requires sudo)
- `make perf-report`: View perf profile
- `make clean`: Remove build artifacts
- `make help`: Print full Makefile usage
- Fuzz is not finished.

### Stress testing

- The project includes a small stress test program (`stress`) intended to exercise the server under high connection/concurrency loads. Build it with `make stress` (or run the built binary directly).

Example usage (one of the presets used during development):

```bash
./stress -count eternal -burst -realistic -nr 1022
```

This will run an eternal burst-style test using all backends. Adjust concurrency and other flags as needed; run `./stress -help` for available options.

### Endpoint test script

- There's a endpoint test script at the project root, it checks for correct replies from all endpoints using curl. `test_endpoints.sh`

Usage:

```bash
./test_endpoints.sh [host] [port]
```

If no host/port are supplied the script will target `localhost:10480`.

### Profiling and Debugging

- **Valgrind**: Use `make profile` to build with DWARF4, then run with Valgrind:
  ```bash
  valgrind --leak-check=full ./server
  ```
- **Perf**: Use `make profile`, then `make perf-record` and `make perf-report`

#### Flamegraphs (perf + FlameGraph)

To get a flamegraph from `perf` (useful to visualise where CPU time is spent):

1. Build a profiling-friendly binary (DWARF symbols, no inlining/optimizations as needed):

```bash
make profile
```

2. Record perf data (sample at e.g. 99Hz, capture call graphs):

```bash
# may need sudo depending on your system
sudo perf record -F 99 -g -- ./server
```

3. Convert perf.data to folded stacks and generate a flamegraph SVG using Brendan Gregg's FlameGraph tools:

```bash
# install FlameGraph if you don't have it
git clone https://github.com/brendangregg/FlameGraph.git /tmp/FlameGraph

# convert perf data to text and collapse stacks
sudo perf script | /tmp/FlameGraph/stackcollapse-perf.pl > out.folded

# generate the flamegraph
/tmp/FlameGraph/flamegraph.pl out.folded > flamegraph.svg

# open flamegraph.svg in a browser or image viewer
```

Notes:

- There are several GUI frontends to inspect the graph data.
- You can use `perf report` interactively to inspect hotspots before making a flamegraph.
- On some distributions you must install `linux-tools-common` / `linux-tools-$(uname -r)` to get `perf`.
- If `perf record` fails due to permission issues, run it with `sudo` or enable perf_event_paranoid (see `man perf`).

## Suggested next steps

### Better UI

Something like this that just tracks info over time.
Can also connect the server to the html page served from index.html.

```
────────────────────────────────── SERVER ──────────────────────────────────
Address:     0.0.0.0:8080
Uptime:      01:23:44
Active:      12 clients
Total:       842 accepted
Failed:      31 total
Cache:       553 hits / 1342 lookups  (41%)

────────────────────────────── BACKEND ACTIVITY ─────────────────────────────
Geocode requests:   417 total   (fail 6)
Meteo requests:     389 total   (fail 3)
Cache:              553 hits / 1342 lookups  (41%)

──────────────────────────────── ERROR COUNTS ──────────────────────────────
GW_OK:              1253
GW_TIMEOUT:         4
GW_HTTP_ERROR:      12
GW_PARSE_FAIL:      6
GW_BAD_REQUEST:     9
GW_INTERNAL:        2

Client errors:      10 total   (parse 4, timeout 3, bad_req 3)
Backend errors:     11 total   (geo 6, meteo 3, internal 2)
```

### Refactor main w_client state machine mega-function

Sugested solution is detailed under/improvments folder

### OS setup from main.c

The server currently throttles because of lack of FDs. Set all these OS settings from main.

### Random list of improvments

- Epoll!
- Timers in the scheduler so tasks can sleep when they are waiting.
- Better cache handling. Kind of wonky now. Just stores first geo lookup hit.
- Filter incoming IPs, we just accept everything now.
- Pools for constantly used data. Clients and served files are currently malloced and freed per connection.

## Known bugs

### "Åre" is cache with correct name but with cordinates somewhere in puerto rico.

### GeoCodeWeather hänger inte med när man kör många requests

Blir JSON parse error. KOlla om vi blir limitade?

### Även när man kör långsamt och träffar cachen så kan man misslyckas, undersök nedanstående

Kommando: "./stress -slow -count 100 -weather"

```bash
Client 93 (active: 2, total: 93) Received 143 bytes (total: 286)
Client 93 (active: 2, total: 93) GET /weather?location=%C3%84lmhult HTTP/1.1
Client 93 (active: 2, total: 93) Request: GET /weather?location=%c3%a4lmhult HTTP/1.1
Client 93 (active: 2, total: 93) Processing request...
[GeocodeWeather] geocache HIT
[GeocodeWeather] ❌ ERROR: weather connect failed
[GeocodeWeather] ❌ ERROR: Connection failed
Client 94 (active: 2, total: 94) Received 141 bytes (total: 282)
Client 94 (active: 2, total: 94) GET /weather?location=Timr%C3%A5 HTTP/1.1
Client 94 (active: 2, total: 94) Request: GET /weather?location=timr%c3%a5 HTTP/1.1
Client 94 (active: 2, total: 94) Processing request...
[GeocodeWeather] geocache HIT
[GeocodeWeather] ❌ ERROR: weather connect failed
[GeocodeWeather] ❌ ERROR: Connection failed
Client 95 (active: 2, total: 95) Received 139 bytes
```

AHA! När man kör:

```bash
curl -i http://localhost:10480/weather?location=timrå
Client    1 (active:    1, total:    1) Received 102 bytes (total: 204)
Client    1 (active:    1, total:    1) GET /weather?location=timrå HTTP/1.1
Client    1 (active:    1, total:    1) Request: GET /weather?location=timrå HTTP/1.1
Client    1 (active:    1, total:    1) Processing request...
          [GeocodeWeather] geocache HIT
          [GeocodeWeather] connected to weather API
          [GeocodeWeather] sent weather request
          [GeocodeWeather] received weather response
          [GeocodeWeather] ✓ completed
```

Notera att timrå strängana är olika "Timr%C3%A5 vs timrå". Jag glömmer nog att koda tillbaks dem om jag träffar cachen. Samma sak med älmhult.

### The debug build segfaults at startup sometimes

Probably a ASAN or sockets issue.

```
jimmy@pop-os:~/progg/chas/UB-WeatherServer$ ./server
Segmentation fault (core dumped)
jimmy@pop-os:~/progg/chas/UB-WeatherServer$ ./server
Segmentation fault (core dumped)
jimmy@pop-os:~/progg/chas/UB-WeatherServer$ ./server

=== UB Weather Server ===

Configuration:
  Bind address : 127.0.0.1
  Port         : 10480
  Note         : Listening on localhost only. Only clients on this machine can connect.
                 To allow external connections, use 0.0.0.0 or the server's network IP.

Available endpoints:
  /weather?location=<x>  - Weather lookup for <x>
  /index.html            - Server monitoring webpage
  /surprise              - Surprise endpoint
  /health                - Returns "OK" if the server is alive
  /                      - Hello message

Server starting...
Listening on 127.0.0.1:10480
Use a client like `curl http://127.0.0.1:10480` to connect
Press Ctrl+C to stop the server

^C
Shutdown signal received. Cleaning up...
Server stopped cleanly.
jimmy@pop-os:~/progg/chas/UB-WeatherServer$
```

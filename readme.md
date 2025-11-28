# UB-WeatherServer

HTTP server that resolves city names to coordinates and serves weather data via (Open‑Meteo).

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

### Endpoints

- `/`: Simple hello text
- `/index.html`: Serves `index.html` from working directory (falls back to embedded HTML)
- `/health`: Liveness check; returns `OK`
- `/weather?location=<name>`: Resolve location name to coordinates and return weather JSON with current and forecast weather
- `/surprise`: Returns a binary surprise

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
- `make stress`: Build stress test binary (`./stress -insane -count 5000`)
- `make fuzz-http` / `make fuzz-client`: Build AFL fuzz harnesses
- `make fuzz-run`: Run fuzz harnesses (requires AFL)
- `make perf-record`: Record perf profile (requires sudo)
- `make perf-report`: View perf profile
- `make clean`: Remove build artifacts
- `make help`: Print full Makefile usage

### Profiling and Debugging

- **Valgrind**: Use `make profile` to build with DWARF4, then run with Valgrind:
  ```bash
  valgrind --leak-check=full ./server
  ```
- **Perf**: Use `make profile`, then `make perf-record` and `make perf-report`

## Notes

- Cache: Geocoding and weather results stored under `cache/`
- Logs: Backend events printed to stderr
- Webroot: Place `index.html` in working directory or `static/` (convention)
- Production: Bind to `0.0.0.0` and run behind a supervisor

License: MIT

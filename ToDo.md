#  TODO — UB-WeatherServer

**For detailed analysis, see:** `ARCHITECTURE_ANALYSIS.md`
## 🔴 Critical Issues (Fix Immediately)

### CPU Usage
- [ ] Replace busy-loop with `epoll_wait()` in `main.c` (currently polling)
- [ ] Add timeout computation from task deadlines
- [ ] Target: <1% CPU when idle

### State Machine Hardening
- [ ] Verify all state transitions in `WeatherServerInstance` are correct
- [ ] Add timeouts for stuck connections (e.g., stalled Work state)
- [ ] Handle partial HTTP reads gracefully

**Status:** Memory leaks fixed as of commit ac7124f. State machines mostly stable.  
**Estimated effort:** 2-3 days  
**Acceptance:** <1% idle CPU, no stuck connections, clean shutdown

## 🟡 High Priority (Next Sprint)

### Error Handling & Logging
- [ ] Add structured logging: `log_msg(level, fmt, ...)`
- [ ] Replace all `printf` with proper log calls
- [ ] Define error code enum
- [ ] Add error context to backend failures (network, timeout, etc.)

### Resource Limits & Robustness
- [ ] Add max concurrent connections limit
- [ ] Add max request size limit
- [ ] Add connection timeout config (60s default)
- [ ] Reject requests when at limit (503 response)
- [ ] Handle partial HTTP reads (incremental parser)

### Backend Improvements
- [ ] Verify all backends (cities, geolocation, weather, surprise) handle errors
- [ ] Add retry logic for transient network failures
- [ ] Add request timeouts for external API calls

**Estimated effort:** 1-2 weeks  
**Acceptance:** Graceful degradation under load, all errors logged with context


## 🟢 Medium Priority (Backlog)

### HTTP Protocol & Compliance
- [ ] Implement incremental parser (handle partial reads)
- [ ] Add HTTP/1.1 compliance checks (Host header, Date header, etc.)
- [ ] Add keep-alive support
- [ ] Validate request headers more strictly

### Application Features
- [ ] Implement routing table (replace strcmp switch in `WeatherServerInstance`)
- [ ] Add middleware chain (logging, auth, rate-limit)
- [ ] Add JSON error response wrapper for all endpoints
- [ ] Document endpoint contracts more formally

### Code Quality
- [ ] Run ASAN (already in debug build) on all tests

## 🔵 Low Priority (Future)
### Configuration
- [ ] Add `server_config_t` struct
- [ ] Load config from file/args
- [ ] Make all constants configurable

## Quick Commands

### Build & Test
```bash
make clean
make MODE=debug
valgrind --leak-check=full ./server
```

### Format Code
```bash
git ls-files '*.c' '*.h' | xargs -r clang-format -i
```

### Check for Leaks
```bash
valgrind --leak-check=full --show-leak-kinds=all ./server
# Run some requests with curl
# Ctrl+C, check summary
```

### Measure CPU Usage
```bash
./server &
top -p $(pgrep server)
# Should be <1% when idle
```
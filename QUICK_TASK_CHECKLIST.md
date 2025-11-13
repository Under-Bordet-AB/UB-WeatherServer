(Prompt claude 4.5: Look through this project and note the design of the implementation. I want you to tell me about all the design patterns that appear and are used in the code. Also comment on alternative solutions and how "canonical" or correct the current use of patterns are. End with a actionable todo list for the whole project divided into topics for easy distribution of tasks on a team.)

# Quick Task Checklist — UB-WeatherServer

**For detailed analysis, see:** `ARCHITECTURE_ANALYSIS.md`

---

## 🔴 Critical Issues (Fix Immediately)

### Memory Leaks
- [ ] Fix connection list leak in `WeatherServer.c` (instances never removed)
- [ ] Fix `writeBuffer` leak in `HTTPServerConnection_SendResponse`
- [ ] Free `url` and `method` strings in `HTTPServerConnection_Dispose`
- [ ] Add valgrind test to verify zero leaks

### CPU Usage
- [ ] Replace `usleep(10000)` busy-loop with `epoll_wait()` in `main.c`
- [ ] Add timeout computation from task deadlines
- [ ] Target: <1% CPU when idle

### State Machine Bug
- [ ] Remove unconditional `state = Done` at end of `WeatherServerInstance_Work`

**Estimated effort:** 3-5 days total  
**Acceptance:** Zero leaks, <1% idle CPU, all states transition correctly

---

## 🟡 High Priority (Next Sprint)

### Scheduler Refactor
- [ ] Replace global `g_smw` with allocated instance
- [ ] Add task return codes: `int callback() -> delay_ms`
- [ ] Add per-task `wakeup_ms` tracking
- [ ] Integrate epoll for event-driven wakeup

### Error Handling
- [ ] Add structured logging: `log_msg(level, fmt, ...)`
- [ ] Replace all `printf` with proper log calls
- [ ] Define error code enum
- [ ] Add error context to all failures

### Resource Limits
- [ ] Add max concurrent connections limit
- [ ] Add max request size limit
- [ ] Add connection timeout config
- [ ] Reject requests when at limit (503 response)

**Estimated effort:** 1-2 weeks  
**Acceptance:** Handles resource exhaustion gracefully, all errors logged

---

## 🟢 Medium Priority (Backlog)

### HTTP Protocol
- [ ] Implement incremental parser (handle partial reads)
- [ ] Add early method validation (reject on first byte)
- [ ] Add keep-alive support
- [ ] Add HTTP/1.1 compliance (Host header, Date header, etc.)

### Application Features
- [ ] Implement routing table (replace strcmp switch)
- [ ] Add middleware chain (logging, auth, rate-limit)
- [ ] Implement `/cities` and `/weather/{city}` routes
- [ ] Add JSON serialization helpers

### Testing
- [ ] Add unit tests for scheduler, state machines, parser
- [ ] Add integration tests for full request cycle
- [ ] Add performance benchmarks (RPS, latency)
- [ ] Add fuzz testing with ASAN

**Estimated effort:** 2-3 weeks  
**Acceptance:** All routes functional, 90%+ test coverage

---

## 🔵 Low Priority (Future)

### Configuration
- [ ] Add `server_config_t` struct
- [ ] Load config from file/args
- [ ] Make all constants configurable

### V2 Migration
- [ ] Port fixes to V2 scaffold
- [ ] Run V2 in parallel for testing
- [ ] Cut over traffic gradually

**Estimated effort:** 3-4 weeks

---

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

---

## Team Assignments (Suggested)

| Team | Topics | Effort |
|------|--------|--------|
| **Infrastructure** | Scheduler refactor, config, limits | 2 weeks |
| **Platform** | Memory cleanup, error handling, logging | 1.5 weeks |
| **Protocol** | HTTP parser, state machines | 1.5 weeks |
| **Application** | Routing, middleware, JSON | 1 week |
| **QA** | Testing, benchmarks, fuzz | Ongoing |

---

## Definition of Done

### Critical Issues
- ✅ Zero memory leaks under valgrind (10k requests)
- ✅ <1% CPU usage when idle
- ✅ All state machines transition correctly
- ✅ No crashes under load (1k concurrent)

### High Priority
- ✅ Scheduler uses epoll (not polling)
- ✅ All errors logged with context
- ✅ Resource limits enforced
- ✅ Graceful degradation at limits

### Medium Priority
- ✅ HTTP/1.1 compliant
- ✅ All routes implemented with tests
- ✅ 90%+ code coverage
- ✅ Performance benchmarks documented

---

**Last updated:** November 8, 2025  
**Next review:** After critical issues resolved

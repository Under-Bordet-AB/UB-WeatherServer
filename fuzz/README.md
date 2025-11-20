Fuzzing guide — fuzz/
=====================

This folder contains harnesses and seed corpora for fuzzing the HTTP parser with AFL/AFL++.

Layout
- `harnesses/` — small harness programs (C) that call internal entry points (e.g. `http_request_fromstring`).
- `corpus/<target>/` — seed inputs used by the fuzzer (small, representative files).
- `out/` — afl output directory (created by afl-fuzz, should be ignored by git).

Quick start (local)
-------------------
1. Install AFL or AFL++ (ensure `afl-clang-fast` and `afl-fuzz` are on PATH).

2. Build the instrumented harness (Makefile helper exists):

```bash
# from repo root
make fuzz-http
```

3. Run afl-fuzz (interactive):

```bash
make fuzz-run
```

Or run in background (nohup):

```bash
make fuzz-run-bg
```

Seeds and dictionaries
----------------------
- Place seeds in `fuzz/corpus/<target>/`. Start with 1–10 small but valid samples.
- Add a dictionary file when you have repeatable protocol tokens (AFL++ supports `-x dict.txt`).

Sanitizers and speed
--------------------
- The harness builds with AddressSanitizer enabled to surface memory errors. ASAN is ideal for triaging crashes but slows execution.
- For long fuzz runs, consider two-phase approach:
  1. Use coverage-instrumented builds without ASAN to discover branches quickly.
  2. Re-run interesting inputs under ASAN for detailed reports.

Persistent mode
---------------
- Harnesses include `__AFL_LOOP` so AFL persistent mode can be used, which reduces fork overhead and increases throughput. Keep inner-loop work minimal.

Crash triage
------------
- AFL saves crashes in `fuzz/out/<target>/crashes/` and hangs in `fuzz/out/<target>/hangs/`.
- Use ASAN reports (appearing on stderr) to find stack traces, and use `afl-tmin`/`afl-cmin` to minimize/testcases.

CI / Automation notes
---------------------
- Fuzzing is long-running. In CI, prefer:
  - Short, deterministic fuzz jobs (few minutes) as smoke tests, or
  - Scheduled/periodic fuzz jobs on dedicated infra (e.g., daily on a runner with enough CPU/RAM).
- Do not run full afl-fuzz in PR jobs.

Cleaning and stopping
---------------------
- Background run pid saved to: `fuzz/out/http_request/afl.pid` (when started with `make fuzz-run-bg`).
- To stop background run:

```bash
kill $(cat fuzz/out/http_request/afl.pid) || true
rm -f fuzz/out/http_request/afl.pid
```

- Remove output (unsafe to store in git):

```bash
rm -rf fuzz/out/http_request
```

Tips
----
- Keep harnesses focused and deterministic — mock network/services when possible.
- Seed small inputs that exercise different code paths (GET, POST, malformed, long fields).
- Monitor CPU and disk; AFL can generate a lot of files.

If you'd like, I can:
- Add a `make fuzz-clean` target to the Makefile to stop background runs and remove `fuzz/out/`.
- Add a dictionary file or a custom mutator for protocol-specific fuzzing.

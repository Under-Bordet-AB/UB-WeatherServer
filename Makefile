# ============================================
#  Makefile with Debug (ASAN), Release, and Profile modes
# ============================================

CC := gcc

# AFL / fuzz build settings
# Use afl-clang-fast by default for instrumented builds; override with AFL_CC
AFL_CC ?= afl-clang-fast
# Fuzz build flags: ASAN + UBSAN-ish flags suitable for fuzzing
FUZZ_CFLAGS ?= -g -O1 -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize-recover=all
FUZZ_INCLUDES ?= -I./src -I./src/libs


# Generate include search paths for ALL folders under src/
SRC_DIRS := $(shell find src -type d)
INCLUDES := -Iinclude $(addprefix -I,$(SRC_DIRS))

# Common flags
CFLAGS_COMMON := -D_POSIX_C_SOURCE=200809L -std=c99 -Wall -Wextra \
                 $(INCLUDES) -MMD -MP \
                 -Wno-unused-parameter -Wno-unused-function -Wno-format-truncation \
                 -Wno-stringop-truncation

# Debug flags (with Address Sanitizer) - DEFAULT
CFLAGS_DEBUG := $(CFLAGS_COMMON) -g -O0 -fsanitize=address -DDEBUG

# Release flags (maximum performance)
CFLAGS_RELEASE := $(CFLAGS_COMMON) -O3 -march=native -DNDEBUG \
                  -fomit-frame-pointer -flto

# Profile flags (optimized with frame pointers for perf)
CFLAGS_PROFILE := $(CFLAGS_COMMON) -O2 -g -fno-omit-frame-pointer -DNDEBUG

LFLAGS_COMMON := -lm
LFLAGS_DEBUG := -fsanitize=address
LFLAGS_RELEASE := -flto
LFLAGS_PROFILE :=

# Default build mode
BUILD_MODE ?= debug

# Set flags based on build mode
ifeq ($(BUILD_MODE),release)
    CFLAGS := $(CFLAGS_RELEASE)
    LFLAGS := $(LFLAGS_COMMON) $(LFLAGS_RELEASE)
    BUILD_DIR := build/release
    BIN := server
else ifeq ($(BUILD_MODE),profile)
    CFLAGS := $(CFLAGS_PROFILE)
    LFLAGS := $(LFLAGS_COMMON) $(LFLAGS_PROFILE)
    BUILD_DIR := build/profile
    BIN := server-profile
else
    CFLAGS := $(CFLAGS_DEBUG)
    LFLAGS := $(LFLAGS_COMMON) $(LFLAGS_DEBUG)
    BUILD_DIR := build/debug
    BIN := server
endif

# Recursive source discovery
SRC := $(shell find src -name '*.c')

# Object mirroring
OBJ := $(patsubst src/%.c,$(BUILD_DIR)/%.o,$(SRC))

# Dependency files
DEP := $(OBJ:.o=.d)

.PHONY: all debug release profile run run-release clean perf-record perf-report stress stress-enhanced help

# Default target - debug with ASAN
all: debug

# Debug build (ASAN enabled)
debug:
	@$(MAKE) BUILD_MODE=debug build

# Release build (maximum performance)
release:
	@$(MAKE) BUILD_MODE=release build

# Profile build (optimized for perf)
profile:
	@$(MAKE) BUILD_MODE=profile build

$(BIN): $(OBJ)
	@mkdir -p $(@D)
	@$(CC) $(CFLAGS) $^ -o $@ $(LFLAGS)
	@echo ""
	@echo "╔══════════════════════════════════╗"
	@echo "║         BUILD SUCCESSFULL         ║"
	@echo "╚══════════════════════════════════╝"
	@echo "      Binary: $(BIN)"
	@echo "      Mode:   $(BUILD_MODE)"
	@echo ""

build: $(BIN)

$(BUILD_DIR)/%.o: src/%.c
	@mkdir -p $(dir $@)
	@$(CC) $(CFLAGS) -c $< -o $@

run: debug
	./$(BIN)

run-release: release
	./server

stress: stress.c
	@echo "Building stress test..."
	@$(CC) -o stress stress.c -O2
	@echo "✓ stress test built successfully"

# Build AFL-instrumented fuzz harness for HTTP parser
.PHONY: fuzz-http
fuzz-http:
	@echo "Building AFL-instrumented http_request fuzz harness..."
	@mkdir -p build
	@if ! command -v $(AFL_CC) >/dev/null 2>&1; then \
		echo "Error: $(AFL_CC) not found. Install AFL or set AFL_CC to your afl-clang-fast."; exit 1; \
	fi
	@$(AFL_CC) $(FUZZ_CFLAGS) $(FUZZ_INCLUDES) -o build/http_request_fuzz \
		fuzz/harnesses/http_request_fuzz.c src/libs/http_parser.c src/libs/linked_list.c
	@echo "✓ fuzz harness built at build/http_request_fuzz"

# Build AFL-instrumented fuzz harness for client parsing
.PHONY: fuzz-client
fuzz-client:
	@echo "Building AFL-instrumented client_parse fuzz harness..."
	@mkdir -p build
	@if ! command -v $(AFL_CC) >/dev/null 2>&1; then \
		echo "Error: $(AFL_CC) not found. Install AFL or set AFL_CC to your afl-clang-fast."; exit 1; \
	fi
	@$(AFL_CC) $(FUZZ_CFLAGS) $(FUZZ_INCLUDES) -o build/client_parse_fuzz \
		fuzz/harnesses/client_parse_fuzz.c src/libs/http_parser.c src/libs/linked_list.c
	@echo "✓ fuzz harness built at build/client_parse_fuzz"

.PHONY: fuzz-run
fuzz-run: fuzz-http
	@echo "Preparing to run afl-fuzz (will run in foreground)."
	@which afl-fuzz >/dev/null 2>&1 || (echo "Error: afl-fuzz not found. Install AFL." && exit 1)
	@mkdir -p fuzz/out/http_request
	@echo "Tip: run this inside tmux or screen if you want to leave it running."
	@echo "Starting afl-fuzz... Press Ctrl+C to stop."
	@ASAN_OPTIONS=detect_leaks=0:abort_on_error=1:allocator_may_return_null=1:symbolize=0 AFL_I_DONT_CARE_ABOUT_MISSING_CRASHES=1 MALLOC_ARENA_MAX=1 \
		afl-fuzz -x fuzz/dict/http.dict -i fuzz/corpus/http_request -o fuzz/out/http_request -m none -t 2000 -- build/http_request_fuzz @@

.PHONY: fuzz-run-bg
fuzz-run-bg: fuzz-http
	@echo "Starting afl-fuzz in background (output in fuzz/out/http_request)."
	@which afl-fuzz >/dev/null 2>&1 || (echo "Error: afl-fuzz not found. Install AFL." && exit 1)
	@mkdir -p fuzz/out/http_request
	@ASAN_OPTIONS=detect_leaks=0:abort_on_error=1:allocator_may_return_null=1:symbolize=0 AFL_I_DONT_CARE_ABOUT_MISSING_CRASHES=1 MALLOC_ARENA_MAX=1 \
		nohup afl-fuzz -x fuzz/dict/http.dict -i fuzz/corpus/http_request -o fuzz/out/http_request -m none -t 2000 -- build/http_request_fuzz @@ > fuzz/out/http_request/nohup.log 2>&1 &
	@echo $! > fuzz/out/http_request/afl.pid
	@echo "afl-fuzz started in background, pid saved to fuzz/out/http_request/afl.pid"

.PHONY: fuzz-client-run
fuzz-client-run: fuzz-client
	@echo "Preparing to run afl-fuzz for client parsing (will run in foreground)."
	@which afl-fuzz >/dev/null 2>&1 || (echo "Error: afl-fuzz not found. Install AFL." && exit 1)
	@mkdir -p fuzz/out/client_parse
	@echo "Tip: run this inside tmux or screen if you want to leave it running."
	@echo "Starting afl-fuzz for client parser... Press Ctrl+C to stop."
	@ASAN_OPTIONS=detect_leaks=0:abort_on_error=1:allocator_may_return_null=1:symbolize=0 AFL_I_DONT_CARE_ABOUT_MISSING_CRASHES=1 MALLOC_ARENA_MAX=1 \
		afl-fuzz -x fuzz/dict/http.dict -i fuzz/corpus/client_parse -o fuzz/out/client_parse -m none -t 2000 -- build/client_parse_fuzz @@

.PHONY: fuzz-client-run-bg
fuzz-client-run-bg: fuzz-client
	@echo "Starting afl-fuzz for client parsing in background (output in fuzz/out/client_parse)."
	@which afl-fuzz >/dev/null 2>&1 || (echo "Error: afl-fuzz not found. Install AFL." && exit 1)
	@mkdir -p fuzz/out/client_parse
	@ASAN_OPTIONS=detect_leaks=0:abort_on_error=1:allocator_may_return_null=1:symbolize=0 AFL_I_DONT_CARE_ABOUT_MISSING_CRASHES=1 MALLOC_ARENA_MAX=1 \
		nohup afl-fuzz -x fuzz/dict/http.dict -i fuzz/corpus/client_parse -o fuzz/out/client_parse -m none -t 2000 -- build/client_parse_fuzz @@ > fuzz/out/client_parse/nohup.log 2>&1 &
	@echo $! > fuzz/out/client_parse/afl.pid
	@echo "afl-fuzz for client parser started in background, pid saved to fuzz/out/client_parse/afl.pid"

.PHONY: fuzz-clean
fuzz-clean:
	@echo "Stopping background afl-fuzz (if running) and cleaning fuzz output..."
	@if [ -f fuzz/out/http_request/afl.pid ]; then \
		pid=$$(cat fuzz/out/http_request/afl.pid) ; \
		if kill -0 $$pid >/dev/null 2>&1; then \
			echo "Killing $$pid"; kill $$pid || true; \
		else \
			echo "No running process with pid $$pid"; \
		fi; \
		rm -f fuzz/out/http_request/afl.pid; \
	fi
	@if [ -f fuzz/out/client_parse/afl.pid ]; then \
		pid=$$(cat fuzz/out/client_parse/afl.pid) ; \
		if kill -0 $$pid >/dev/null 2>&1; then \
			echo "Killing $$pid"; kill $$pid || true; \
		else \
			echo "No running process with pid $$pid"; \
		fi; \
		rm -f fuzz/out/client_parse/afl.pid; \
	fi
	@rm -rf fuzz/out/http_request fuzz/out/client_parse
	@echo "Removed fuzz output directories (and any logs)."

perf-record: profile
	@echo "========================================="
	@echo "  Starting perf profiling session"
	@echo "========================================="
	@echo ""
	@echo "Server will start with perf recording..."
	@echo "Run your stress tests in another terminal:"
	@echo "  ./stress -insane -count 5000"
	@echo ""
	@echo "Press Ctrl+C when done to stop recording."
	@echo ""
	@sleep 2
	sudo perf record -g --call-graph dwarf -F 999 ./server-profile

perf-report:
	@if [ ! -f perf.data ]; then \
		echo "Error: perf.data not found!"; \
		echo "Run 'make perf-record' first."; \
		exit 1; \
	fi
	@echo "========================================="
	@echo "  Opening interactive perf report"
	@echo "========================================="
	@echo ""
	@echo "Navigation:"
	@echo "  ↑↓    - Move through functions"
	@echo "  Enter - Expand call graph"
	@echo "  q     - Quit"
	@echo ""
	@sleep 2
	sudo perf report

clean:
	@echo "Cleaning build artifacts..."
	$(RM) -rf build $(BIN) server-profile stress perf.data perf.data.old
	@echo "✓ Clean complete"

help:
	@echo "========================================="
	@echo "  Server Build System"
	@echo "========================================="
	@echo ""
	@echo "Build Targets:"
	@echo "  make              - Build debug version with ASAN (default)"
	@echo "  make debug        - Build debug version with ASAN"
	@echo "  make release      - Build optimized release version"
	@echo "  make profile      - Build optimized for profiling"
	@echo ""
	@echo "Run Targets:"
	@echo "  make run          - Build and run debug version"
	@echo "  make run-release  - Build and run release version"
	@echo ""
	@echo "Testing:"
	@echo "  make stress          - Build enhanced REST API stress test"
	@echo ""
	@echo "Fuzzing:"
	@echo "  make fuzz-http       - Build AFL-instrumented HTTP parser fuzz harness (build/http_request_fuzz)"
	@echo "  make fuzz-run        - Build harness and run afl-fuzz in foreground (interactive)"
	@echo "  make fuzz-run-bg     - Build harness and start afl-fuzz in background (nohup/log + pidfile)"
	@echo "  make fuzz-client     - Build AFL-instrumented client parser fuzz harness (build/client_parse_fuzz)"
	@echo "  make fuzz-client-run - Build client harness and run afl-fuzz in foreground"
	@echo "  make fuzz-client-run-bg - Build client harness and start afl-fuzz in background"
	@echo "  make fuzz-clean      - Stop all background fuzzers and clean output"
	@echo "  Fuzz corpus: fuzz/corpus/http_request/ and fuzz/corpus/client_parse/"
	@echo ""
	@echo "Profiling Targets (Linux only):"
	@echo "  make perf-record  - Start server with perf recording"
	@echo "  make perf-report  - View profiling results"
	@echo ""
	@echo "Other:"
	@echo "  make clean        - Remove all build artifacts"
	@echo "  make help         - Show this help message"
	@echo ""
	@echo "========================================="
	@echo "  Build Modes"
	@echo "========================================="
	@echo ""
	@echo "debug (default):"
	@echo "  • -O0 -g -fsanitize=address"
	@echo "  • Catches memory errors, buffer overflows, use-after-free"
	@echo "  • Slow but safe for development"
	@echo ""
	@echo "release:"
	@echo "  • -O3 -march=native -flto"
	@echo "  • Maximum performance, no debug info"
	@echo "  • Use for production and benchmarking"
	@echo ""
	@echo "profile:"
	@echo "  • -O2 -g -fno-omit-frame-pointer"
	@echo "  • Optimized but keeps frame pointers for perf"
	@echo "  • Use with 'perf' to find bottlenecks"
	@echo ""
	@echo "========================================="
	@echo "  Profiling Workflow"
	@echo "========================================="
	@echo ""
	@echo "1. Start profiling:"
	@echo "     make perf-record"
	@echo ""
	@echo "2. In another terminal, run your stress test:"
	@echo "     ./stress_test -insane -count 5000"
	@echo ""
	@echo "3. Stop the server with Ctrl+C"
	@echo ""
	@echo "4. View results:"
	@echo "     make perf-report"
	@echo ""
	@echo "5. Navigate the report:"
	@echo "     • Look for functions with high 'Overhead %'"
	@echo "     • Press Enter to see call graph"
	@echo "     • Focus on optimizing functions >5% overhead"
	@echo ""
	@echo "========================================="

-include $(DEP)
# ============================================
#  Makefile with Debug (ASAN), Release, and Profile modes
# ============================================

CC := gcc

# Generate include search paths for ALL folders under src/
SRC_DIRS := $(shell find src -type d)
INCLUDES := -Iinclude $(addprefix -I,$(SRC_DIRS))

# Common flags
CFLAGS_COMMON := -D_POSIX_C_SOURCE=200809L -std=c99 -Wall -Wextra \
                 $(INCLUDES) -MMD -MP \
                 -Wno-unused-parameter -Wno-unused-function -Wno-format-truncation

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
	@$(MAKE) BUILD_MODE=debug $(BIN)

# Release build (maximum performance)
release:
	@$(MAKE) BUILD_MODE=release $(BIN)

# Profile build (optimized for perf)
profile:
	@$(MAKE) BUILD_MODE=profile $(BIN)

$(BIN): $(OBJ)
	@mkdir -p $(@D)
	@$(CC) $(CFLAGS) $^ -o $@ $(LFLAGS)
	@echo ""
	@echo "╔══════════════════════════════════╗"
	@echo "║         BUILD SUCCESSFUL         ║"
	@echo "╚══════════════════════════════════╝"
	@echo "      Binary: $(BIN)"
	@echo "      Mode:   $(BUILD_MODE)"
	@echo ""

$(BUILD_DIR)/%.o: src/%.c
	@mkdir -p $(dir $@)
	@$(CC) $(CFLAGS) -c $< -o $@

run: debug
	./$(BIN)

run-release: release
	./$(BIN)

stress: stress_test.c
	@echo "Building stress test..."
	@$(CC) -o stress stress_test.c -O2
	@echo "✓ stress test built successfully"

stress-enhanced: stress_test_enhanced.c
	@echo "Building enhanced stress test..."
	@$(CC) -o stress-enhanced stress_test_enhanced.c -O2
	@echo "✓ enhanced stress test built successfully"

perf-record: profile
	@echo "========================================="
	@echo "  Starting perf profiling session"
	@echo "========================================="
	@echo ""
	@echo "Server will start with perf recording..."
	@echo "Run your stress tests in another terminal:"
	@echo "  ./stress_test -insane -count 5000"
	@echo ""
	@echo "Press Ctrl+C when done to stop recording."
	@echo ""
	@sleep 2
	sudo perf record -g --call-graph dwarf -F 999 ./$(BIN)

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
	$(RM) -rf build $(BIN) server-profile stress stress-enhanced perf.data perf.data.old
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
	@echo "  make stress          - Build basic stress test (outputs 'stress')"
	@echo "  make stress-enhanced - Build enhanced REST API stress test"
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
# Variables
CC=gcc
OPTIMIZE=-ffunction-sections -fdata-sections -O2 -flto -Wno-unused-result -fno-strict-aliasing
DEBUG_FLAGS=-g -O0 -Wfatal-errors -Werror -Wno-unused-function -Wno-format-truncation
LIBS=-lcurl -pthread -lm
INCLUDES = -I. -Isrc -Iinclude -Ilibs -Ilibs/jansson

#   -DWALLOCATOR_DEBUG -DWALLOCATOR_DEBUG_BORDERCHECK
# AddressSanitizer flags (used for debug builds)
SANITIZE_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer

# Build mode: release (default) or debug
MODE ?= debug

# Base warnings/defs
CFLAGS_BASE=-Wall -Wno-psabi -Wfatal-errors -Werror

# Select flags per mode
ifeq ($(MODE),debug)
	CFLAGS=$(CFLAGS_BASE) $(DEBUG_FLAGS) $(SANITIZE_FLAGS)
	LDFLAGS=$(SANITIZE_FLAGS)
else
	CFLAGS=$(CFLAGS_BASE) #$(OPTIMIZE)
	LDFLAGS=
endif

# Directories
SRC_DIR=.
BUILD_DIR=build
CACHE_DIR=cache

# Find all .c files (following symlinks)
SOURCES=$(shell find -L $(SRC_DIR) -type f -name '*.c')

# Ignore stress.c in normal builds
SOURCES_NO_STRESS := $(filter-out $(SRC_DIR)/stress.c, $(SOURCES))

# Per-target object lists in separate dirs
SERVER_OBJECTS=$(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/server/%.o,$(SOURCES_NO_STRESS))
CLIENT_OBJECTS=$(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/client/%.o,$(SOURCES_NO_STRESS))

# Executables
EXECUTABLES=server client stress

# Default target
all: $(EXECUTABLES)
	@echo "Build complete ($(MODE))."
	@mkdir -p $(CACHE_DIR)/cities $(CACHE_DIR)/weather

# Debug helpers
debug-server:
	@$(MAKE) MODE=debug --no-print-directory clean server
	@-rm -f WADEBUG.txt

debug-client:
	@$(MAKE) MODE=debug --no-print-directory clean client
	@-rm -f WADEBUG.txt

# Convenience target: build everything with AddressSanitizer enabled
.PHONY: asan debug-stress
asan:
	@echo "Building with AddressSanitizer (MODE=debug)..."
	@$(MAKE) MODE=debug --no-print-directory clean all

# Build the standalone stress binary in debug mode (with ASan)
debug-stress:
	@$(MAKE) MODE=debug --no-print-directory clean stress
	@echo "Built stress (debug/ASan): ./stress"

# Link rules
server: $(SERVER_OBJECTS)
	@echo "Linking $@..."
	@$(CC) $(LDFLAGS) $^ -o $@ $(LIBS)

client: $(CLIENT_OBJECTS)
	@echo "Linking $@..."
	@$(CC) $(LDFLAGS) $^ -o $@ $(LIBS)

# Standalone stress build
stress: $(BUILD_DIR)/stress.o
	@echo "Linking stress..."
	@$(CC) $(LDFLAGS) $^ -o stress $(LIBS)

$(BUILD_DIR)/stress.o: stress.c
	@echo "Compiling stress.c..."
	@mkdir -p $(BUILD_DIR)
	@$(CC) $(CFLAGS) $(INCLUDES) -c stress.c -o $(BUILD_DIR)/stress.o

# Compile rules with per-target defines
$(BUILD_DIR)/server/%.o: $(SRC_DIR)/%.c
	@echo "Compiling (server) $<..."
	@mkdir -p $(dir $@)
	@$(CC) $(CFLAGS) $(INCLUDES) -DTCPSERVER -c $< -o $@

$(BUILD_DIR)/client/%.o: $(SRC_DIR)/%.c
	@echo "Compiling (client) $<..."
	@mkdir -p $(dir $@)
	@$(CC) $(CFLAGS) $(INCLUDES) -DTCPCLIENT -c $< -o $@

# Specific file compilation
FILE=
compile:
	@echo "Compiling $(FILE) (server defs)..."
	@mkdir -p $(BUILD_DIR)/server/$(dir $(FILE))
	$(CC) $(CFLAGS) $(INCLUDES) -DTCPSERVER -c $(FILE) -o $(BUILD_DIR)/server/$(FILE:.c=.o)

# Clean
clean:
	@echo "Cleaning up..."
	@rm -rf $(BUILD_DIR) server client stress

.PHONY: all clean compile debug-server debug-client stress

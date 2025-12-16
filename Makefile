# Variables
CC=gcc
OPTIMIZE=-ffunction-sections -fdata-sections -O2 -flto -Wno-unused-result -fno-strict-aliasing
DEBUG_FLAGS=-g -O0 -Werror -Wno-unused-function -Wno-format-truncation
MBEDTLS_DIR=mbedtls
LIBS=-lcurl -pthread -lm  -I mbedtls/include
INCLUDES = -I. -Isrc -Iinclude -Ilibs -Ilibs/jansson -I$(MBEDTLS_DIR)/include
SANITIZE_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer

MODE ?= debug

# Base warnings/defs
CFLAGS_BASE=-Wall -Wno-psabi -Werror -Wformat-truncation -DMBEDTLS_CONFIG_FILE='"mbedtls_config.h"'

# Select flags per mode
ifeq ($(MODE),debug)
    CFLAGS=$(SANITIZE_FLAGS) $(CFLAGS_BASE) $(DEBUG_FLAGS)
    LDFLAGS=$(SANITIZE_FLAGS)
else
    CFLAGS=$(CFLAGS_BASE) $(OPTIMIZE)
    LDFLAGS=
endif

# Directories
SRC_DIR=.
BUILD_DIR=build
CACHE_DIR=cache

# Find all .c files (following symlinks)
SOURCES=$(shell find -L $(SRC_DIR) -type f -name '*.c')

# Per-target object lists in separate dirs
SERVER_OBJECTS=$(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/server/%.o,$(SOURCES))

# Executables
EXECUTABLES=server

all: $(EXECUTABLES)
	@echo "Build complete ($(MODE))."
	@mkdir -p $(CACHE_DIR)/cities $(CACHE_DIR)/weather

asan:
	@echo "Building with AddressSanitizer (MODE=debug)..."
	@$(MAKE) MODE=debug --no-print-directory clean all

# Link rules
server: $(SERVER_OBJECTS)
	@echo "Linking $@..."
	@$(CC) $(LDFLAGS) $^ -o $@ $(LIBS)

# Compile rules with per-target defines
$(BUILD_DIR)/server/%.o: $(SRC_DIR)/%.c
	@echo "Compiling (server) $<..."
	@mkdir -p $(dir $@)
	@$(CC) $(CFLAGS) $(INCLUDES) -DTCPSERVER -c $< -o $@

# Clean
clean:
	@echo "Cleaning up..."
	@rm -rf $(BUILD_DIR) server client stress

.PHONY: all clean compile debug-server debug-client stress

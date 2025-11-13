# This makefile builds a debug build with ASAN and debug flags

CC := gcc
CFLAGS := -D_POSIX_C_SOURCE=200809L -g -Wall -Wextra -std=c99 \
          -Iinclude -Isrc/libs -Isrc/ -I. \
          -MMD -MP -Wno-unused-parameter -Wno-unused-function -Wno-format-truncation -fsanitize=address
LFLAGS :=

# --- Configuration ---
BUILD_DIR := build
BIN := server

# --- Application Source Discovery (recursive) ---
SRC := $(shell find src -name '*.c')
OBJ := $(patsubst src/%.c,$(BUILD_DIR)/%.o,$(SRC))

# --- Dependency Files ---
DEP := $(OBJ:.o=.d)

.PHONY: all run clean

all: $(BIN)

# ====================================================================
# APPLICATION BUILD RULES
# ====================================================================

$(BIN): $(OBJ)
	@mkdir -p $(@D)
	@echo "LINKING all object files into $@"
	$(CC) $(CFLAGS) $^ -o $@ $(LFLAGS)

# Match any .c file in any subdirectory of src/
$(BUILD_DIR)/%.o: src/%.c
	@mkdir -p $(@D)
	@echo "COMPILING $<"
	$(CC) $(CFLAGS) -c $< -o $@

# ====================================================================
# UTILITY TARGETS
# ====================================================================

run: $(BIN)
	./$(BIN)

clean:
	@echo "CLEANING $(BUILD_DIR) and $(BIN)"
	$(RM) -rf $(BUILD_DIR) $(BIN)

# Include automatically generated dependency files
-include $(DEP)

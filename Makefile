# ============================================
#  Debug build with ASAN
# ============================================

CC := gcc

# Generate include search paths for ALL folders under src/
SRC_DIRS := $(shell find src -type d)
CFLAGS := -D_POSIX_C_SOURCE=200809L -g -Wall -Wextra -std=c99 \
          -Iinclude $(addprefix -I,$(SRC_DIRS)) \
          -MMD -MP \
          -Wno-unused-parameter -Wno-unused-function -Wno-format-truncation \
          -fsanitize=address
LFLAGS :=

BUILD_DIR := build
BIN := server

# Recursive source discovery
SRC := $(shell find src -name '*.c')

# Object mirroring
OBJ := $(patsubst src/%.c,$(BUILD_DIR)/%.o,$(SRC))

# Dependency files
DEP := $(OBJ:.o=.d)

.PHONY: all run clean

all: $(BIN)

$(BIN): $(OBJ)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $^ -o $@ $(LFLAGS)

$(BUILD_DIR)/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

run: $(BIN)
	./$(BIN)

clean:
	$(RM) -rf $(BUILD_DIR) $(BIN)

-include $(DEP)

# Compiler
CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -Iincludes
LDFLAGS = -lmicrohttpd -ljson-c -lcurl

# Detect Homebrew include/lib paths
HOMEBREW_PREFIX := $(shell brew --prefix)
CFLAGS += -I$(HOMEBREW_PREFIX)/include
LDFLAGS += -L$(HOMEBREW_PREFIX)/lib

# Directories
SRC_DIR = src
BUILD_DIR = build

# Sources
SRC = $(wildcard $(SRC_DIR)/*.c) main.c
OUT = $(BUILD_DIR)/server

# Targets
all: $(OUT)

$(OUT): $(SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(SRC) -o $(OUT) $(LDFLAGS)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

clean:
	rm -rf $(BUILD_DIR)/*

.PHONY: all clean

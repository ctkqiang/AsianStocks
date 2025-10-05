# Compiler and flags
CC      = gcc
CFLAGS  = -I/usr/include -Iincludes -Wall -Wextra -std=c11
LDFLAGS = -lmicrohttpd -ljson-c -lcurl

# Directories
SRC_DIR    = src
BUILD_DIR  = build
INCLUDE_DIR = includes

# Source files
SRC        = $(wildcard $(SRC_DIR)/*.c)
OUT        = $(BUILD_DIR)/server

# Targets
all: $(OUT)

$(OUT): $(SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(SRC) -o $(OUT) $(LDFLAGS)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

clean:
	rm -rf $(BUILD_DIR)/*

.PHONY: all clean

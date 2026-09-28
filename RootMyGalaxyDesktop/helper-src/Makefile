NDK ?= $(HOME)/Android/Sdk/ndk/28.2.13676358
CC := $(NDK)/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android35-clang
CFLAGS := -fPIE -pie -O2 -g0 -Wall -Wextra

PROJECT_ROOT := $(abspath $(CURDIR)/../..)
HELPER_DIR := $(PROJECT_ROOT)/targets/zzi8-WIP/helper
OUTPUT := helper-readable

.PHONY: all clean

all: $(OUTPUT)

# Build the same ZZI8 helper staged by simple-root.
$(OUTPUT): $(HELPER_DIR)/su_daemon.c
	$(CC) $(CFLAGS) $(HELPER_DIR)/su_daemon.c -ldl -o $@

clean:
	rm -f $(OUTPUT)

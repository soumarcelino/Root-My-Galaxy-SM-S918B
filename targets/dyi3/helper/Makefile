ANDROID_NDK_HOME ?= $(HOME)/Android/Sdk/ndk/28.2.13676358
API ?= 35
TARGET_CC := $(ANDROID_NDK_HOME)/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android$(API)-clang
NDK_FALLBACK := $(HOME)/Android/Sdk/ndk/28.2.13676358
ifeq ($(wildcard $(TARGET_CC)),)
TARGET_CC := $(NDK_FALLBACK)/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android$(API)-clang
endif
SOURCE := su_daemon.c
OUTPUT := build/cve-2026-43499-root

.PHONY: all clean
all: $(OUTPUT)

$(OUTPUT): $(SOURCE)
	mkdir -p build
	$(TARGET_CC) -fPIE -pie -O2 -g0 -Wall -Wextra $(SOURCE) -ldl -o $@

clean:
	rm -rf build

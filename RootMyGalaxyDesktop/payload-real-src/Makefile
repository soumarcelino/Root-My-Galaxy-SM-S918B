NDK ?= $(HOME)/Android/Sdk/ndk/28.2.13676358
API ?= 35
TARGET ?= dm3q-S918BXXSAFZG1
CC := $(NDK)/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android$(API)-clang
TARGET_INCLUDE := targets/$(TARGET)/target.h
OUTPUT ?= build/cve-2026-43499-app-$(TARGET).so
SLIDE_SOURCE := src/slide_app.c
ifeq ($(TARGET),dm3q-S918BXXSAFZG1)
SLIDE_SOURCE := src/slide.c
endif
SOURCES := \
	src/main.c \
	src/util.c \
	$(SLIDE_SOURCE) \
	src/fops.c \
	src/pipe.c \
	src/root.c \
	src/preload.c
CFLAGS := -DAPP_PAYLOAD=1 -fPIC -O2 -g0 -Wall -Wextra \
	-Wno-unused-parameter -Wno-sign-compare -Isrc \
	-DTARGET_HEADER='"$(TARGET_INCLUDE)"'

.PHONY: all check

all: $(OUTPUT)

$(OUTPUT): $(SOURCES) src/common.h src/offset.h \
		src/kernelsnitch/futex_hash.h src/kernelsnitch/kernelsnitch.h \
		src/kernelsnitch/timeutils.h src/kernelsnitch/utils.h \
		src/targets/$(TARGET)/target.h \
		src/targets/$(TARGET)/p0_fingerprint.h
	mkdir -p $(@D)
	$(CC) $(CFLAGS) $(SOURCES) -shared -pthread -o $@

check: $(OUTPUT)
	file $(OUTPUT)
	readelf -h $(OUTPUT) | grep -E 'Class:|Data:|Type:|Machine:'
	sha256sum $(OUTPUT)

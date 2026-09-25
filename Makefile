API ?= 35
TARGET ?= dm3q-S918BXXSAFZF5
OUTDIR ?= build/$(TARGET)

ifeq ($(TARGET),dm3q-S918BXXSAFZH3)
$(error AFZH3 payload is built from targets/afzh3/open-payload-engine)
else ifeq ($(TARGET),dm3q-S918BXXUAZZHL)
TARGET_DIR := targets/zzhl-WIP
else ifeq ($(TARGET),ZZHL)
TARGET_DIR := targets/zzhl-WIP
else
TARGET_DIR := targets/afzf5
endif
SRC_DIR := $(TARGET_DIR)/payload/src
HELPER_SRC := $(TARGET_DIR)/helper/su_daemon.c
TARGET_HEADER := $(SRC_DIR)/targets/$(TARGET)/target.h
TARGET_INCLUDE := targets/$(TARGET)/target.h
TARGET_CC := $(ANDROID_NDK_HOME)/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android$(API)-clang

ifeq ($(wildcard $(TARGET_CC)),)
$(error set ANDROID_NDK_HOME to an Android NDK containing $(TARGET_CC))
endif

PRELOAD := $(OUTDIR)/cve-2026-43499
APP_PRELOAD := $(OUTDIR)/cve-2026-43499-app.so
APP_RELEASE := $(OUTDIR)/cve-2026-43499-app.release.so
APP_RELEASE_SIZE := 104128
ROOT_HELPER := $(OUTDIR)/cve-2026-43499-root

PRELOAD_SRCS := \
  $(SRC_DIR)/main.c \
  $(SRC_DIR)/util.c \
  $(SRC_DIR)/slide.c \
  $(SRC_DIR)/fops.c \
  $(SRC_DIR)/pipe.c \
  $(SRC_DIR)/root.c \
  $(SRC_DIR)/preload.c

APP_PRELOAD_SRCS := \
  $(SRC_DIR)/main.c \
  $(SRC_DIR)/util.c \
  $(SRC_DIR)/slide_app.c \
  $(SRC_DIR)/fops.c \
  $(SRC_DIR)/pipe.c \
  $(SRC_DIR)/root.c \
  $(SRC_DIR)/preload.c

COMMON_CFLAGS := \
  -O2 -g0 -Wall -Wextra \
  -Wno-unused-parameter -Wno-sign-compare \
  -I$(SRC_DIR) -DTARGET_HEADER='"$(TARGET_INCLUDE)"'

.DEFAULT_GOAL := all

.PHONY: all clean info release

all: $(PRELOAD) $(APP_PRELOAD) $(ROOT_HELPER)

release: $(APP_RELEASE)

$(OUTDIR):
	mkdir -p $@

$(PRELOAD): $(PRELOAD_SRCS) $(TARGET_HEADER) $(SRC_DIR)/offset.h $(SRC_DIR)/common.h $(SRC_DIR)/kernelsnitch/*.h | $(OUTDIR)
	$(TARGET_CC) -fPIC $(COMMON_CFLAGS) $(PRELOAD_SRCS) \
	  -shared -pthread -o $@

$(ROOT_HELPER): $(HELPER_SRC) | $(OUTDIR)
	$(TARGET_CC) -fPIE -pie -O2 -g0 -Wall -Wextra $< -ldl -o $@

$(APP_PRELOAD): $(APP_PRELOAD_SRCS) $(TARGET_HEADER) $(SRC_DIR)/offset.h $(SRC_DIR)/common.h $(SRC_DIR)/kernelsnitch/*.h | $(OUTDIR)
	$(TARGET_CC) -DAPP_PAYLOAD=1 -fPIC $(COMMON_CFLAGS) $(APP_PRELOAD_SRCS) \
	  -shared -pthread -o $@

$(APP_RELEASE): $(APP_PRELOAD_SRCS) $(TARGET_HEADER) $(SRC_DIR)/offset.h $(SRC_DIR)/common.h $(SRC_DIR)/kernelsnitch/*.h | $(OUTDIR)
	$(TARGET_CC) -DAPP_PAYLOAD=1 -fPIC -Oz -g0 \
	  -fno-unwind-tables -fno-asynchronous-unwind-tables \
	  -ffunction-sections -fdata-sections \
	  -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare \
	  -I$(SRC_DIR) -DTARGET_HEADER='"$(TARGET_INCLUDE)"' \
	  $(APP_PRELOAD_SRCS) -shared -pthread \
	  -Wl,--gc-sections -Wl,--icf=all -s -o $@
	@test $$(stat -c %s $@) -le $(APP_RELEASE_SIZE)
	truncate -s $(APP_RELEASE_SIZE) $@

info:
	@echo "TARGET=$(TARGET)"
	@echo "TARGET_CC=$(TARGET_CC)"
	@echo "PRELOAD=$(PRELOAD)"
	@echo "APP_PRELOAD=$(APP_PRELOAD)"
	@echo "APP_RELEASE=$(APP_RELEASE)"
	@echo "ROOT_HELPER=$(ROOT_HELPER)"

clean:
	rm -rf $(OUTDIR)

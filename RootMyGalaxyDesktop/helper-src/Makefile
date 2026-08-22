NDK ?= $(HOME)/Android/Sdk/ndk/28.2.13676358
CC := $(NDK)/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android35-clang
CFLAGS := -std=c11 -O2 -Wall -Wextra -Werror

HELPER_DIR := $(CURDIR)/port/helper-next
OUTPUT := helper-readable

.PHONY: all clean

all: $(OUTPUT)

# Esta e a mesma unidade C ligada ao monolito por build_monolithic.py.
$(OUTPUT): $(HELPER_DIR)/su_daemon.c $(HELPER_DIR)/target_guard.c \
	$(HELPER_DIR)/target_guard.h $(HELPER_DIR)/target-afzg1.h
	$(CC) $(CFLAGS) -I$(HELPER_DIR) \
		$(HELPER_DIR)/su_daemon.c $(HELPER_DIR)/target_guard.c \
		-ldl -o $@

clean:
	rm -f $(OUTPUT)

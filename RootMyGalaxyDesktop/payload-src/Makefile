NDK ?= $(HOME)/Android/Sdk/ndk/28.2.13676358
CC := $(NDK)/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android35-clang
HOST_CC ?= cc
CFLAGS := -std=c11 -O2 -fPIC -Wall -Wextra -Werror -I.
SOURCES := payload.c config.c slide.c attempt.c
OUTPUT := libcve43499root-reconstructed.so
LOADER := payload-test-loader
DIAGNOSTIC := payload-diagnostic
SIMULATION_TEST := payload-simulation-test

.PHONY: all loader diagnostic simulation-test check clean

all: $(OUTPUT)

loader: $(LOADER)

diagnostic: $(DIAGNOSTIC)

simulation-test: $(SIMULATION_TEST)
	./$(SIMULATION_TEST)

check: $(OUTPUT) $(DIAGNOSTIC) $(SIMULATION_TEST)
	./$(DIAGNOSTIC) $(OUTPUT)
	./$(SIMULATION_TEST)

$(OUTPUT): $(SOURCES) include/payload.h
	$(CC) $(CFLAGS) -shared -Wl,-z,relro,-z,now $(SOURCES) -o $@

$(LOADER): test-loader.c
	$(CC) $(CFLAGS) test-loader.c -ldl -o $@

$(DIAGNOSTIC): diagnostic_loader.c
	$(HOST_CC) -D_GNU_SOURCE -std=c11 -O2 -Wall -Wextra -Werror $< -o $@

$(SIMULATION_TEST): attempt.c simulation_test.c include/payload.h
	$(HOST_CC) -std=c11 -O2 -Wall -Wextra -Werror -I. \
		attempt.c simulation_test.c -o $@

clean:
	rm -f $(OUTPUT) $(LOADER) $(DIAGNOSTIC) $(SIMULATION_TEST)

API ?= 35
OUTDIR := build
TARGET_CC := $(ANDROID_NDK_HOME)/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android$(API)-clang
ANDROID_NDK_FALLBACK ?= $(HOME)/Android/Sdk/ndk/28.2.13676358

ifeq ($(wildcard $(TARGET_CC)),)
ifneq ($(wildcard $(ANDROID_NDK_FALLBACK)/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android$(API)-clang),)
ANDROID_NDK_HOME := $(ANDROID_NDK_FALLBACK)
TARGET_CC := $(ANDROID_NDK_HOME)/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android$(API)-clang
endif
endif

ifeq ($(wildcard $(TARGET_CC)),)
$(error set ANDROID_NDK_HOME to an Android NDK containing $(TARGET_CC))
endif

BIN := $(OUTDIR)/app_main
SOBIN := $(OUTDIR)/payload.so

.PHONY: all so tests test-aar-read-plan test-fops-layout clean
all: $(BIN)
so: $(SOBIN)

$(OUTDIR):
	mkdir -p $@

SRCS := src/00_orchestrator.c src/01_kernel_base_tracefs.c src/02_slab_cache_probe.c src/04_fake_kernel_objects.c src/05_mm_slab_grooming.c src/06_signal_frame_payload.c src/07_futex_pi_trigger.c src/08_ashmem_configfs_rw.c src/09_pipe_buffer_rw.c src/10_workqueue_umh_root.c

$(BIN) $(SOBIN): src/90_diagnostic_checkpoint.h src/03_mm_address_sidechannel/mm_address_leak.h

$(BIN): $(SRCS) | $(OUTDIR)
	$(TARGET_CC) -fPIE -pie -O2 -g0 -Wall -Wextra -Wno-unused-parameter -Isrc $(SRCS) -pthread -o $@

# LD_PRELOAD-loadable build: same sources, run via constructor instead of
# main(), same calling convention as the simple-root payload asset
# (LD_PRELOAD=this.so exec /system/bin/true).
$(SOBIN): $(SRCS) | $(OUTDIR)
	$(TARGET_CC) -shared -fPIC -fvisibility=hidden -DBUILD_LD_PRELOAD=1 -O2 -g0 -Wall -Wextra -Wno-unused-parameter -Isrc $(SRCS) -pthread -o $@

# Focused diagnostics live outside src/ and never enter the payload build.
TEST_OUTDIR := $(OUTDIR)/tests
TEST_CFLAGS := -O2 -g0 -Wall -Wextra -Wno-unused-parameter -Isrc -Itests/support -pthread
TEST_BINS := test_groom test_futex_trigger test_mm_leak test_pipe_spray test_signal_sanity test_sigusr1 test_skb_send

tests: $(addprefix $(TEST_OUTDIR)/,$(TEST_BINS))

test-aar-read-plan: | $(TEST_OUTDIR)
	$(or $(HOST_CC),cc) -O2 -Wall -Wextra -Wno-format-truncation tests/test_aar_read_plan.c -o $(TEST_OUTDIR)/test_aar_read_plan_host
	$(TEST_OUTDIR)/test_aar_read_plan_host

test-fops-layout: | $(TEST_OUTDIR)
	$(or $(HOST_CC),cc) -O2 -Wall -Wextra -Werror -Isrc tests/test_fops_layout.c src/04_fake_kernel_objects.c -o $(TEST_OUTDIR)/test_fops_layout_host
	$(TEST_OUTDIR)/test_fops_layout_host

$(TEST_OUTDIR):
	mkdir -p $@

$(TEST_OUTDIR)/test_groom: tests/test_groom.c src/05_mm_slab_grooming.c src/04_fake_kernel_objects.c src/02_slab_cache_probe.c | $(TEST_OUTDIR)
	$(TARGET_CC) $(TEST_CFLAGS) $^ -o $@

$(TEST_OUTDIR)/test_futex_trigger: tests/test_futex_trigger.c src/05_mm_slab_grooming.c src/04_fake_kernel_objects.c src/02_slab_cache_probe.c src/06_signal_frame_payload.c src/07_futex_pi_trigger.c src/08_ashmem_configfs_rw.c src/09_pipe_buffer_rw.c | $(TEST_OUTDIR)
	$(TARGET_CC) $(TEST_CFLAGS) $^ -o $@

$(TEST_OUTDIR)/test_mm_leak: tests/test_mm_leak.c tests/support/mm_leak.c | $(TEST_OUTDIR)
	$(TARGET_CC) $(TEST_CFLAGS) $^ -o $@

$(TEST_OUTDIR)/test_pipe_spray: tests/test_pipe_spray.c tests/support/pipe_spray.c | $(TEST_OUTDIR)
	$(TARGET_CC) $(TEST_CFLAGS) $^ -o $@

$(TEST_OUTDIR)/test_signal_sanity: tests/test_signal_sanity.c | $(TEST_OUTDIR)
	$(TARGET_CC) $(TEST_CFLAGS) $^ -o $@

$(TEST_OUTDIR)/test_sigusr1: tests/test_sigusr1.c src/06_signal_frame_payload.c | $(TEST_OUTDIR)
	$(TARGET_CC) $(TEST_CFLAGS) $^ -o $@

$(TEST_OUTDIR)/test_skb_send: tests/test_skb_send.c tests/support/mm_leak.c tests/support/skb_send_probe.c src/04_fake_kernel_objects.c | $(TEST_OUTDIR)
	$(TARGET_CC) $(TEST_CFLAGS) $^ -o $@

clean:
	rm -rf $(OUTDIR)

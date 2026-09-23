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
SOBIN := $(OUTDIR)/oss_clone_payload.so
P0BIN := $(OUTDIR)/oss_clone_p0_zzhl.so
P0_UPSTREAM ?= ../
P0_LAUNCHER_SRC ?= ../../ksu-payload-functional/stability-launcher.c
P0_LAUNCHER := $(OUTDIR)/stability-launcher-zzhl

.PHONY: all so p0 p0-launcher tests test-aar-read-plan test-fops-layout test-dentry-fops-preflight test-dentry-pipe-transport test-groom-candidates test-futex-multiphase test-futex-lock-witness test-futex-witness clean
all: $(BIN)
so: $(SOBIN)
p0: $(P0BIN) $(P0_LAUNCHER)
p0-launcher: $(P0_LAUNCHER)

$(OUTDIR):
	mkdir -p $@

SRCS := src/main.c src/kaslr.c src/slabinfo.c src/stage_stability.c src/fops_install.c src/fops_spray.c src/groom.c src/futex_trigger.c src/sigusr1_payload.c src/aar_aaw.c src/pipe_physrw.c src/root_umh.c src/dentry_fops_preflight.c src/dentry_pipe_transport.c src/futex_witness.c

$(BIN) $(SOBIN): src/diag_checkpoint.h src/target_zzhl.h

$(BIN): $(SRCS) | $(OUTDIR)
	$(TARGET_CC) -fPIE -pie -O2 -g0 -Wall -Wextra -Wno-unused-parameter -Isrc $(SRCS) -pthread -o $@

# LD_PRELOAD-loadable build: same sources, run via constructor instead of
# main(), same calling convention as ksu-payload-functional/assets/ksu-payload
# (LD_PRELOAD=this.so exec /system/bin/true).
$(SOBIN): $(SRCS) | $(OUTDIR)
	$(TARGET_CC) -shared -fPIC -fvisibility=hidden -DBUILD_LD_PRELOAD=1 -O2 -g0 -Wall -Wextra -Wno-unused-parameter -Isrc $(SRCS) -pthread -o $@

# Separate P0 route.  It reuses the project's open physical-oracle engine,
# but gets its target constants through src/p0/target_zzhl_p0.h.  The legacy
# groom/futex route above remains the default artifact and is not linked here.
P0_SRCS := $(P0_UPSTREAM)/src/main.c $(P0_UPSTREAM)/src/util.c \
	$(P0_UPSTREAM)/src/slide_app.c $(P0_UPSTREAM)/src/fops.c \
	$(P0_UPSTREAM)/src/pipe.c $(P0_UPSTREAM)/src/root.c \
	$(P0_UPSTREAM)/src/preload.c


$(P0BIN): $(P0_SRCS) src/p0/target_zzhl_p0.h src/p0/p0_zzhl_safety.h src/target_zzhl.h \
	tools/audit-p0-zzhl.py tools/audit-p0-rb-geometry.py | $(OUTDIR)
	python3 tools/audit-p0-zzhl.py --upstream $(P0_UPSTREAM)/src/targets/dm3q-S918BXXUAZZHL/target.h --wrapper src/p0/target_zzhl_p0.h
	python3 tools/audit-p0-rb-geometry.py
	$(TARGET_CC) -DAPP_PAYLOAD=1 -shared -fPIC -O2 -g0 -Wall -Wextra \
		-Wno-unused-parameter -Wno-sign-compare -Isrc -I$(P0_UPSTREAM)/src \
		-include src/p0/p0_zzhl_safety.h -DTARGET_HEADER='"p0/target_zzhl_p0.h"' $(P0_SRCS) -pthread -o $@

$(P0_LAUNCHER): $(P0_LAUNCHER_SRC) | $(OUTDIR)
	$(TARGET_CC) -O2 -Wall -Wextra -Werror -fPIE -fstack-protector-strong \
		-D_FORTIFY_SOURCE=2 -DREL_MAX_MM_SLABS=54L -DREL_GATE_NAME='"zzhl-54"' \
		$< -pie -Wl,-z,relro,-z,now -o $@

# Focused diagnostics live outside src/ and never enter the payload build.
TEST_OUTDIR := $(OUTDIR)/tests
TEST_CFLAGS := -O2 -g0 -Wall -Wextra -Wno-unused-parameter -Isrc -Itests/support -pthread
TEST_BINS := test_groom test_futex_trigger test_mm_leak test_pipe_spray test_signal_sanity test_sigusr1 test_spray

tests: $(addprefix $(TEST_OUTDIR)/,$(TEST_BINS))

test-aar-read-plan: | $(TEST_OUTDIR)
	$(or $(HOST_CC),cc) -O2 -Wall -Wextra -Wno-format-truncation tests/test_aar_read_plan.c -o $(TEST_OUTDIR)/test_aar_read_plan_host
	$(TEST_OUTDIR)/test_aar_read_plan_host

test-fops-layout: | $(TEST_OUTDIR)
	$(or $(HOST_CC),cc) -O2 -Wall -Wextra -Isrc tests/test_fops_layout.c src/fops_install.c -o $(TEST_OUTDIR)/test_fops_layout_host
	$(TEST_OUTDIR)/test_fops_layout_host

test-dentry-fops-preflight: | $(TEST_OUTDIR)
	$(or $(HOST_CC),cc) -O2 -Wall -Wextra -Isrc tests/test_dentry_fops_preflight.c src/dentry_fops_preflight.c -o $(TEST_OUTDIR)/test_dentry_fops_preflight_host
	$(TEST_OUTDIR)/test_dentry_fops_preflight_host

test-dentry-pipe-transport: | $(TEST_OUTDIR)
	$(or $(HOST_CC),cc) -O2 -Wall -Wextra -Isrc tests/test_dentry_pipe_transport.c src/dentry_pipe_transport.c -o $(TEST_OUTDIR)/test_dentry_pipe_transport_host
	$(TEST_OUTDIR)/test_dentry_pipe_transport_host

test-groom-candidates: | $(TEST_OUTDIR)
	$(or $(HOST_CC),cc) -O2 -Wall -Wextra -Isrc tests/test_groom_candidates.c src/dentry_fops_preflight.c -o $(TEST_OUTDIR)/test_groom_candidates_host
	$(TEST_OUTDIR)/test_groom_candidates_host

test-futex-multiphase: | $(TEST_OUTDIR)
	$(or $(HOST_CC),cc) -O2 -Wall -Wextra -Isrc tests/test_futex_multiphase.c src/dentry_fops_preflight.c -o $(TEST_OUTDIR)/test_futex_multiphase_host
	$(TEST_OUTDIR)/test_futex_multiphase_host

test-futex-lock-witness: | $(TEST_OUTDIR)
	$(TARGET_CC) $(TEST_CFLAGS) tests/test_futex_lock_witness.c -o $(TEST_OUTDIR)/test_futex_lock_witness

test-futex-witness: | $(TEST_OUTDIR)
	$(or $(HOST_CC),cc) -O2 -Wall -Wextra -Isrc tests/test_futex_witness.c src/futex_witness.c -o $(TEST_OUTDIR)/test_futex_witness_host
	$(TEST_OUTDIR)/test_futex_witness_host

$(TEST_OUTDIR):
	mkdir -p $@

$(TEST_OUTDIR)/test_groom: tests/test_groom.c src/groom.c src/fops_install.c src/fops_spray.c | $(TEST_OUTDIR)
	$(TARGET_CC) $(TEST_CFLAGS) $^ -o $@

$(TEST_OUTDIR)/test_futex_trigger: tests/test_futex_trigger.c src/groom.c src/fops_install.c src/fops_spray.c src/futex_trigger.c src/sigusr1_payload.c src/pipe_physrw.c src/aar_aaw.c | $(TEST_OUTDIR)
	$(TARGET_CC) $(TEST_CFLAGS) $^ -o $@

$(TEST_OUTDIR)/test_mm_leak: tests/test_mm_leak.c tests/support/mm_leak.c | $(TEST_OUTDIR)
	$(TARGET_CC) $(TEST_CFLAGS) $^ -o $@

$(TEST_OUTDIR)/test_pipe_spray: tests/test_pipe_spray.c tests/support/pipe_spray.c | $(TEST_OUTDIR)
	$(TARGET_CC) $(TEST_CFLAGS) $^ -o $@

$(TEST_OUTDIR)/test_signal_sanity: tests/test_signal_sanity.c | $(TEST_OUTDIR)
	$(TARGET_CC) $(TEST_CFLAGS) $^ -o $@

$(TEST_OUTDIR)/test_sigusr1: tests/test_sigusr1.c src/sigusr1_payload.c | $(TEST_OUTDIR)
	$(TARGET_CC) $(TEST_CFLAGS) $^ -o $@

$(TEST_OUTDIR)/test_spray: tests/test_spray.c tests/support/mm_leak.c src/fops_install.c src/fops_spray.c | $(TEST_OUTDIR)
	$(TARGET_CC) $(TEST_CFLAGS) $^ -o $@

clean:
	rm -rf $(OUTDIR)

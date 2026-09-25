#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "fops_install.h"

static uint64_t get64(const unsigned char *buf, size_t off) {
  uint64_t value;
  memcpy(&value, buf + off, sizeof(value));
  return value;
}

int main(void) {
  static unsigned char object[FOPS_INSTALL_PAGE_SIZE];
  const uint64_t base = 0xffffff8036760000ULL;
  const uint64_t kernel = 0xffffffc008000000ULL;
  const uint64_t ashmem = kernel + 0x02bfcf28ULL;
  const uint64_t init_task = kernel + 0x02c05080ULL;

  build_fops_install_object(object, base, kernel, ashmem, init_task);
  if (get64(object, 0x2000) != 0 ||
      get64(object, 0x2008) != (base | 0x14e8ULL) ||
      get64(object, 0x2218) != (base | 0x14d0ULL) ||
      get64(object, 0x2220) != (base | 0x14d0ULL) ||
      get64(object, 0x2228) != 1) {
    fprintf(stderr, "fake fops layout mismatch\n");
    return 1;
  }
  puts("fake fops owner/layout ok");
  return 0;
}

/* Host-only regression proof for the configfs AAR position calculation. */
#define _GNU_SOURCE
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "../src/08_ashmem_configfs_rw.c"

static void check(uint64_t target, size_t len, int expect_large_step) {
  uint64_t base = target - (OSS_CONFIGFS_COUNT - len);
  uint64_t page = next_nonzero_bytes(base);
  uint64_t displacement = page - base;
  uint64_t offset = OSS_CONFIGFS_COUNT - len - displacement;
  for (unsigned int shift = 0; shift < 64; shift += 8) {
    assert(((page >> shift) & 0xffU) != 0);
  }
  assert(page >= base);
  assert(displacement <= OSS_CONFIGFS_COUNT - len - 1);
  assert(page + offset == target);
  assert((displacement >= 0x100) == expect_large_step);
}

int main(void) {
  check(0xffffff88304aa180ULL, 35, 1);
  check(0xffffff88302e2180ULL, 35, 1);
  check(0xffffff8a2f99a180ULL, 35, 1);
  check(0xffffff8a52aaa180ULL, 35, 0);
  check(0xffffffc00ad64f28ULL, 8, 0);
  puts("PASS AAR read positions");
  return 0;
}

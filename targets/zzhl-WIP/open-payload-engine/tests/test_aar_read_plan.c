#define _GNU_SOURCE
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../src/08_ashmem_configfs_rw.c"

static void check_supported_read(uint64_t target, size_t len) {
  struct configfs_read_plan plan;
  assert(make_configfs_read_plan(target, len, &plan));
  assert(plan.page + plan.offset == target);
  assert(plan.kernel_check_len == len);
  assert(OSS_CONFIGFS_COUNT - plan.offset == len);

  assert(oss_kernel_read_plan_supported(target, len));
}

static void check_rejected_read(uint64_t target, size_t len,
                                uint64_t expected_encoded_page) {
  struct configfs_read_plan plan;
  unsigned char control[OSS_CONFIGFS_CONTROL_LEN];
  unsigned char encoded[OSS_CONFIGFS_CONTROL_LEN];
  assert(make_configfs_read_plan(target, len, &plan));
  memset(control, 1, sizeof(control));
  memcpy(control + OSS_CONFIGFS_PAGE_CONTROL_OFF, &plan.page,
         sizeof(plan.page));
  memset(control + OSS_CONFIGFS_READ_STATE_CONTROL_OFF, 0, 0x34);
  simulate_ashmem_name_blob(control, sizeof(control), encoded);
  uint64_t encoded_page = 0;
  memcpy(&encoded_page, encoded + OSS_CONFIGFS_PAGE_CONTROL_OFF,
         sizeof(encoded_page));
  assert(encoded_page == expected_encoded_page);
  errno = 0;
  assert(!oss_kernel_read_plan_supported(target, len));
  assert(errno == EILSEQ);
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
  /* Successful app execution: both verification reads are representable. */
  check_supported_read(0xffffff8a41f4a180ULL, 35);
  check_supported_read(0xffffffc00ad64f28ULL, 8);

  /* Failed app execution: SELinux read silently lost page byte 0x73. */
  check_rejected_read(0xffffffc00ae96600ULL, 1,
                      0x92978c5edb00009dULL);

  /* Panic execution: scratch read lost the high five page bytes. */
  check_rejected_read(0xffffff8a30372180ULL, 35, 0x0000000000c0bc3fULL);
  assert(oss_verify_kernel_access_plan_supported(0xffffffc00ad64f28ULL,
                                                  0xffffff8a41f48000ULL));
  errno = 0;
  assert(!oss_verify_kernel_access_plan_supported(0xffffffc00ad64f28ULL,
                                                   0xffffff8a30370000ULL));
  assert(errno == EILSEQ);

  /* AAW controls used by those same executions remain exactly encodable. */
  assert(oss_kernel_write_plan_supported(0xffffffc00ae96600ULL, 1));
  assert(oss_kernel_write_plan_supported(0xffffff8a30372180ULL, 35));
  assert(oss_kernel_write_plan_supported(0xffffff895399b000ULL, 40));

  reproduce_crash_geometry();

  struct configfs_read_plan rejected;
  assert(!make_configfs_read_plan(UINT64_MAX - 3, 8, &rejected));
  assert(!make_configfs_read_plan(0xffffffc00ad64f28ULL, 0, &rejected));
  puts("PASS exact AAR plans and ZZHL word-at-a-time strscpy rejection");
  return 0;
}

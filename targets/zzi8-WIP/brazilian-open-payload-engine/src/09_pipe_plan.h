#ifndef OSS_CLONE_PIPE_PLAN_H
#define OSS_CLONE_PIPE_PLAN_H

#include <stddef.h>
#include <stdint.h>

#include "target.h"

#define OSS_PIPE_PLAN_MAX_CANDIDATES \
  (TARGET_ORDER3_SIZE / TARGET_PIPE_OBJECT_SIZE)

struct oss_pipe_plan {
  uint64_t known_order3_base;
  uint64_t candidates[OSS_PIPE_PLAN_MAX_CANDIDATES];
  size_t candidate_count;
  int ready;
};

int oss_pipe_plan_build(uint64_t known_order3_base, uint64_t payload_base,
                        struct oss_pipe_plan *plan);
int oss_pipe_plan_contains(const struct oss_pipe_plan *plan, uint64_t address);

#endif

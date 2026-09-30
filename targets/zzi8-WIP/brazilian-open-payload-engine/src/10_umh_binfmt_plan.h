#ifndef BOPE_UMH_BINFMT_PLAN_H
#define BOPE_UMH_BINFMT_PLAN_H

#include <stddef.h>
#include <stdint.h>

#include "target.h"

#define OSS_BINFMT_TRIGGER_SIZE 4U
#define OSS_BINFMT_MODULE_NAME_SIZE 16U

struct oss_umh_binfmt_plan {
  uint32_t uid;
  uint8_t trigger[OSS_BINFMT_TRIGGER_SIZE];
  char module_name[OSS_BINFMT_MODULE_NAME_SIZE];
  uint8_t replacement[TARGET_STATIC_UMH_PATCH_SIZE];
};

extern const uint8_t
    oss_umh_static_guard[TARGET_STATIC_UMH_GUARD_SIZE];

int oss_umh_binfmt_plan_build(uint32_t uid,
                              struct oss_umh_binfmt_plan *plan);
int oss_umh_static_guard_valid(const void *bytes, size_t length);

#endif

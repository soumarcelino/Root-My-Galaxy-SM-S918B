#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "10_umh_binfmt_plan.h"

int main(void) {
  struct oss_umh_binfmt_plan plan;
  assert(!oss_umh_binfmt_plan_build(0, &plan));
  assert(!oss_umh_binfmt_plan_build(UINT16_MAX + 1U, &plan));
  assert(oss_umh_binfmt_plan_build(10037U, &plan));
  assert(plan.uid == 10037U);
  assert(plan.trigger[0] == 0xff && plan.trigger[1] == 0xff);
  assert(plan.trigger[2] == 0x35 && plan.trigger[3] == 0x27);
  assert(strcmp(plan.module_name, "binfmt-2735") == 0);
  assert(memcmp(plan.replacement, "/data/local/tmp/r\0",
                TARGET_STATIC_UMH_PATCH_SIZE) == 0);
  assert(oss_umh_static_guard_valid(oss_umh_static_guard,
                                    sizeof(oss_umh_static_guard)));
  uint8_t damaged[TARGET_STATIC_UMH_GUARD_SIZE];
  memcpy(damaged, oss_umh_static_guard, sizeof(damaged));
  damaged[0] = '/';
  assert(!oss_umh_static_guard_valid(damaged, sizeof(damaged)));
  assert(TARGET_STATIC_UMH_PATH_OFF == 0x01e14b34ULL);
  puts("umh binfmt plan regression: ok");
  return 0;
}

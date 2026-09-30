#include "10_umh_binfmt_plan.h"

#include <stdio.h>
#include <string.h>

const uint8_t oss_umh_static_guard[TARGET_STATIC_UMH_GUARD_SIZE] = {
    0x00, 'n', 'f', 'c', '_', 'l', 'l', 'c', 'p', '_', 's', 'e',
    'n',  'd', '_', 'u', 'i', '_', 'f', 'r', 'a', 'm', 'e', 0x00,
};

_Static_assert(sizeof(TARGET_STATIC_UMH_PATH) ==
                   TARGET_STATIC_UMH_PATCH_SIZE,
               "static UMH replacement must include its terminating NUL");
_Static_assert(sizeof(oss_umh_static_guard) == TARGET_STATIC_UMH_GUARD_SIZE,
               "static UMH guard size");

int oss_umh_binfmt_plan_build(uint32_t uid,
                              struct oss_umh_binfmt_plan *plan) {
  if (!plan || uid == 0 || uid > UINT16_MAX) {
    return 0;
  }
  memset(plan, 0, sizeof(*plan));
  plan->uid = uid;
  plan->trigger[0] = 0xff;
  plan->trigger[1] = 0xff;
  plan->trigger[2] = (uint8_t)(uid & 0xffU);
  plan->trigger[3] = (uint8_t)(uid >> 8);
  int count = snprintf(plan->module_name, sizeof(plan->module_name),
                       "binfmt-%04x", (unsigned int)uid);
  if (count != 11) {
    return 0;
  }
  memcpy(plan->replacement, TARGET_STATIC_UMH_PATH,
         sizeof(plan->replacement));
  return 1;
}

int oss_umh_static_guard_valid(const void *bytes, size_t length) {
  return bytes && length == sizeof(oss_umh_static_guard) &&
         memcmp(bytes, oss_umh_static_guard,
                sizeof(oss_umh_static_guard)) == 0;
}

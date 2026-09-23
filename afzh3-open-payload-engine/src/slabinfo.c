/* source: FUN_00106ec0 @ 0x0010a6c0 in cve-2026-43499-app-afzh3.so
 * (Ghidra decompile, this session). Straight port -- fopen/fgets/memcmp
 * loop to find the "mm_struct " line, then one sscanf. No control-flow
 * ambiguity in the decompile for this function (unlike FUN_00103e18,
 * this one was not affected by the BOLT "unreachable block" bug -- it is
 * small enough Ghidra's analysis handled it directly, and independently
 * cross-checked against the format string bytes read out of the binary's
 * own .rodata: "mm_struct %lu %lu %lu %lu %lu : tunables %*lu %*lu %*lu
 * : slabdata %lu %lu %lu". */
#include <stdio.h>
#include <string.h>

#include "slabinfo.h"

int read_mm_slabinfo(struct mm_slabinfo *out) {
  FILE *fp = fopen("/proc/slabinfo", "re");
  if (!fp) {
    return 0;
  }
  char line[512];
  int found = 0;
  while (fgets(line, sizeof(line), fp)) {
    if (memcmp(line, "mm_struct ", 10) != 0) {
      continue;
    }
    int matched = sscanf(
        line,
        "mm_struct %lu %lu %lu %lu %lu : tunables %*lu %*lu %*lu : "
        "slabdata %lu %lu %lu",
        &out->active_objs, &out->num_objs, &out->objsize,
        &out->objperslab, &out->pagesperslab, &out->active_slabs,
        &out->num_slabs, &out->shared_avail);
    found = matched == 8;
    break;
  }
  fclose(fp);
  return found;
}

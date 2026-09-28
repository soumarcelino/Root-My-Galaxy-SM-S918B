/*
 * Reads named slab-cache geometry and occupancy from /proc/slabinfo. Parses
 * the mm_struct cache for preflight and reclaim checks.
 */

#include <stdio.h>
#include <string.h>

#include "02_slab_cache_probe.h"

int read_named_slabinfo(const char *name, struct mm_slabinfo *out) {
  if (!name || !*name || !out) {
    return 0;
  }
  FILE *fp = fopen("/proc/slabinfo", "re");
  if (!fp) {
    return 0;
  }
  char line[512];
  int found = 0;
  size_t name_len = strlen(name);
  while (fgets(line, sizeof(line), fp)) {
    if (strncmp(line, name, name_len) != 0 || line[name_len] != ' ') {
      continue;
    }
    int matched = sscanf(
        line + name_len,
        " %lu %lu %lu %lu %lu : tunables %*lu %*lu %*lu : "
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

int read_mm_slabinfo(struct mm_slabinfo *out) {
  return read_named_slabinfo("mm_struct", out);
}

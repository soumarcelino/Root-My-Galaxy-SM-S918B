/*
 * Defines slab-cache counters and declares the named and mm_struct probes.
 */
#ifndef OSS_CLONE_SLAB_CACHE_PROBE_H
#define OSS_CLONE_SLAB_CACHE_PROBE_H

#include <stdint.h>

struct mm_slabinfo {
  unsigned long active_objs;
  unsigned long num_objs;
  unsigned long objsize;
  unsigned long objperslab;
  unsigned long pagesperslab;
  unsigned long active_slabs;
  unsigned long num_slabs;
  unsigned long shared_avail;
};

int read_mm_slabinfo(struct mm_slabinfo *out);
int read_named_slabinfo(const char *name, struct mm_slabinfo *out);

#endif

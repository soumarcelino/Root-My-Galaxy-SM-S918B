#ifndef OSS_CLONE_SLABINFO_H
#define OSS_CLONE_SLABINFO_H

#include <stdint.h>

/* source: FUN_00106ec0 @ 0x0010a6c0. Parses the "mm_struct" line of
 * /proc/slabinfo. Returns 1 on success (line found and parsed), 0 if the
 * file could not be opened or the line was never found -- matches the
 * closed binary's own fclose-then-return-fclose()'s-int behavior, which
 * effectively only used this for its (harmless) side effect of failing
 * softly; we surface the parsed fields since milestone 4 will need them
 * to decide whether a spray attempt landed. */
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

#endif

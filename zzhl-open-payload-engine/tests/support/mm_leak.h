#ifndef OSS_CLONE_MM_LEAK_H
#define OSS_CLONE_MM_LEAK_H

#include <stdint.h>

/* Leaks this process's own mm_struct kernel address via the kernelsnitch
 * futex-hash timing side channel (src/kernelsnitch/kernelsnitch.h, reused
 * verbatim from the project's already-verified open engine -- this is a
 * generic, self-contained side channel, not part of the buggy
 * exploit-specific code path).
 *
 * source: this is the mechanism FUN_00106288 in cve-2026-43499-app-afzh3.so
 * uses for its own mm_struct leak -- confirmed by matching its
 * FUN_001052a0 cleanup routine's munmap() call pattern against
 * kernelsnitch_cleanup()'s struct layout (futexes field at offset 0x58,
 * size FUTEX_SZ = 0x1000000000 = 64GB, matches exactly).
 *
 * Read-only: never writes to any real kernel object, only measures
 * private futex wake timings. Safe to call standalone. */
int leak_own_mm_struct(uint64_t *mm_struct_out);

#endif

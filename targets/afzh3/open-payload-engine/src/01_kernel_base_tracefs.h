#ifndef OSS_CLONE_KERNEL_BASE_TRACEFS_H
#define OSS_CLONE_KERNEL_BASE_TRACEFS_H

#include <stdint.h>

/* source: FUN_0010757c / FUN_0010597c. Read-only: never writes to any
 * kernel object, only toggles tracefs knobs and reads trace_pipe_raw.
 * Returns 1 and fills *kernel_base_out with the live (slid) kernel .text
 * base on success, 0 on failure. */
int kaslr_locate_via_tracefs(uint64_t *kernel_base_out);

#endif

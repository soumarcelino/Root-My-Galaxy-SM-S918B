/*
 * Declares tracefs-based kernel base discovery and its success/failure
 * contract.
 */
#ifndef OSS_CLONE_KERNEL_BASE_TRACEFS_H
#define OSS_CLONE_KERNEL_BASE_TRACEFS_H

#include <stdint.h>

int kaslr_locate_via_tracefs(uint64_t *kernel_base_out);

#endif

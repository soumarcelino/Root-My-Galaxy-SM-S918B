/*
 * Declares the mm_struct grooming and reclaim operation and its
 * aligned-address result.
 */
#ifndef OSS_CLONE_MM_SLAB_GROOMING_H
#define OSS_CLONE_MM_SLAB_GROOMING_H

#include <stdint.h>

uint64_t groom_and_install_fops_object(uint64_t kernel_base,
                                        uint64_t ashmem_misc_fops_addr,
                                        uint64_t init_task_addr);

#endif

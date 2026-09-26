/*
 * Defines the reclaimed buffer size and declares construction of the fake
 * kernel-object layout.
 */
#ifndef OSS_CLONE_FAKE_KERNEL_OBJECTS_H
#define OSS_CLONE_FAKE_KERNEL_OBJECTS_H

#include <stdint.h>

#define FOPS_INSTALL_PAGE_SIZE 0x8e80

void build_fops_install_object(unsigned char *scratch, uint64_t aligned_base,
                                uint64_t kernel_base,
                                uint64_t ashmem_misc_fops_addr,
                                uint64_t init_task_addr);

#endif

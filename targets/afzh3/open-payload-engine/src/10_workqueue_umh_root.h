/*
 * Declares the workqueue-based usermode-helper root operation and its
 * irreversible-state tracking contract.
 */
#ifndef OSS_CLONE_WORKQUEUE_UMH_ROOT_H
#define OSS_CLONE_WORKQUEUE_UMH_ROOT_H

#include <stdint.h>

/* Runs the workqueue-hijack root grant after oss_pipe_rw_install(). The caller
 * retains ownership of the verified ashmem fd and closes it after this call. */
int root_umh_install_fd(int fd, uint64_t kernel_base, uint64_t page_base,
                        const char *root_umh_path);

/* Tracked production entry point. Immediately before the first live
 * workqueue counter write, publishes irreversible_state with release
 * ordering. After publication, callers must not retry or tear down holders in
 * the same boot, even when this function returns 0. */
int root_umh_install_fd_tracked(int fd, uint64_t kernel_base,
                                uint64_t page_base,
                                const char *root_umh_path,
                                int32_t *kernel_state,
                                int32_t irreversible_state);

int root_umh_install(uint64_t kernel_base, uint64_t page_base,
                      const char *root_umh_path);

#endif

/*
 * Declares the native workqueue usermode-helper operation and its
 * irreversible-state tracking contract.
 */
#ifndef OSS_CLONE_WORKQUEUE_UMH_ROOT_H
#define OSS_CLONE_WORKQUEUE_UMH_ROOT_H

#include <stdint.h>

/* Runs the private-PTY root grant after oss_pipe_rw_install(). The kernel's
 * schedule_work() path owns workqueue locking, list insertion and counters. */
int root_umh_install_fd(int fd, uint64_t kernel_base, uint64_t page_base,
                        uint64_t memstart_addr, uint64_t kimage_voffset,
                        const char *root_umh_path);

/* Tracked production entry point. Immediately before publishing the temporary
 * PTY work object, publishes irreversible_state with release ordering. After
 * publication, callers must not retry or tear down holders in the same boot,
 * even when this function returns 0. */
int root_umh_install_fd_tracked(int fd, uint64_t kernel_base,
                                uint64_t page_base,
                                uint64_t memstart_addr,
                                uint64_t kimage_voffset,
                                const char *root_umh_path,
                                int32_t *kernel_state,
                                int32_t irreversible_state);

int root_umh_install(uint64_t kernel_base, uint64_t page_base,
                      const char *root_umh_path);

#endif

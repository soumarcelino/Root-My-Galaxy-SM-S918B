/* Standalone, ISOLATED test for sigusr1_payload.c -- installs the
 * handler, builds a payload with fake (harmless, userspace-only)
 * addresses, fires tgkill(self, SIGUSR1), and reports whether the
 * handler found the FPSIMD record and copied the payload in. No
 * groom.c, no futex, no real kernel addresses, no ashmem -- this ONLY
 * exercises the signal handler mechanics in isolation, safe to run
 * anywhere before ever combining it with anything that touches the
 * kernel. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "sigusr1_payload.h"

static uint64_t get64(const uint8_t *image, size_t offset) {
  uint64_t value;
  memcpy(&value, image + offset, sizeof(value));
  return value;
}

static int test_waiter_phase_layout(void) {
  static const uint64_t parent = 0x1111111111111111ULL;
  static const uint64_t right = 0x2222222222222222ULL;
  static const uint64_t left = 0x3333333333333333ULL;
  static const uint64_t task = 0x4444444444444444ULL;
  static const uint64_t lock = 0x5555555555555555ULL;
  static const uint32_t prio = 0x82;
  uint8_t image[SIGUSR1_PAYLOAD_SIZE];
  uint8_t expected[SIGUSR1_PAYLOAD_SIZE] = {0};

  sigusr1_build_waiter_phase_image(image, parent, right, left, task, lock,
                                   prio);
  memcpy(expected + 0x30, &parent, sizeof(parent));
  memcpy(expected + 0x38, &right, sizeof(right));
  memcpy(expected + 0x40, &left, sizeof(left));
  memcpy(expected + 0x48, &task, sizeof(task));
  memcpy(expected + 0x50, &lock, sizeof(lock));
  {
    const uint64_t packed_prio = (uint64_t)prio << 32;
    memcpy(expected + 0x58, &packed_prio, sizeof(packed_prio));
  }

  if (memcmp(image, expected, sizeof(image)) != 0 ||
      get64(image, 0x28) != 0 || get64(image, 0x30) != parent ||
      get64(image, 0x48) != task || get64(image, 0x50) != lock) {
    fprintf(stderr, "waiter phase layout mismatch\n");
    return 0;
  }
  return 1;
}

static int test_settle_phase_layout(void) {
  static const uint64_t task = 0xffffffc00add8b38ULL;
  static const uint64_t lock = 0xffffffc00add6b38ULL;
  uint8_t image[SIGUSR1_PAYLOAD_SIZE];

  sigusr1_build_waiter_phase_image(image, 0, 0, 0, task, lock, 0);
  if (get64(image, 0x30) != 0 || get64(image, 0x38) != 0 ||
      get64(image, 0x40) != 0 || get64(image, 0x48) != task ||
      get64(image, 0x50) != lock || get64(image, 0x58) != 0) {
    fprintf(stderr, "settle phase layout mismatch\n");
    return 0;
  }
  return 1;
}

int main(void) {
  if (!test_waiter_phase_layout()) {
    return 1;
  }
  if (!test_settle_phase_layout()) {
    return 1;
  }
  if (!sigusr1_install_handler()) {
    fprintf(stderr, "install_handler failed\n");
    return 1;
  }
  sigusr1_build_payload(0xdeadbeef000ULL, 0xcafebabe000ULL);

  int ok = sigusr1_fire_and_wait();
  printf("handler_ok=%d\n", ok);
  return ok ? 0 : 1;
}

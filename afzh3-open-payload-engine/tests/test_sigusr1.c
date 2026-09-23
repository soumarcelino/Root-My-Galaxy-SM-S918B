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

#include "sigusr1_payload.h"

int main(void) {
  if (!sigusr1_install_handler()) {
    fprintf(stderr, "install_handler failed\n");
    return 1;
  }
  sigusr1_build_payload(0xdeadbeef000ULL, 0xcafebabe000ULL);

  int ok = sigusr1_fire_and_wait();
  printf("handler_ok=%d\n", ok);
  return ok ? 0 : 1;
}

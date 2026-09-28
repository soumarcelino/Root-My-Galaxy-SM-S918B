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

#include "06_signal_frame_payload.h"

int main(void) {
  if (!sigusr1_install_handler()) {
    fprintf(stderr, "install_handler failed\n");
    return 1;
  }
  sigusr1_build_payload(0xdeadbeef000ULL, 0xcafebabe000ULL);
  int primary_ok = sigusr1_fire_and_wait();
  sigusr1_build_pointer_write_payload(0xdeadbeef000ULL, 0xcafebabe000ULL,
                                      0xfeedface000ULL);
  int recovery_ok = sigusr1_fire_and_wait();
  sigusr1_build_null_write_payload(0xdeadbeef000ULL, 0xcafebabe000ULL);
  int quarantine_ok = sigusr1_fire_and_wait();
  printf("primary_ok=%d recovery_ok=%d quarantine_ok=%d\n", primary_ok,
         recovery_ok, quarantine_ok);
  return primary_ok && recovery_ok && quarantine_ok ? 0 : 1;
}

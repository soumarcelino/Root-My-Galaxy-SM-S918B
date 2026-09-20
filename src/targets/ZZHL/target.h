#ifndef ZZHL_TARGET_H
#define ZZHL_TARGET_H

/* ZZHL reuses the common target layout; only kernel identity and verified
 * ZZHL-specific numeric values are overridden here. */
#include "../dm3q-S918BXXSAFZF5/target.h"

#undef BUILD_VARIANT_LABEL
#define BUILD_VARIANT_LABEL "ZZHL-root-umh"

#undef BUILD_FINGERPRINT
#define BUILD_FINGERPRINT "samsung/ZZHL/ZZHL:5.15.189/ZZHL:user/release-keys"

#undef P0_FINGERPRINT_HEADER
#define P0_FINGERPRINT_HEADER "targets/ZZHL/p0_fingerprint.h"

#undef MM_STRUCT_SZ
#define MM_STRUCT_SZ 0x3e0

#undef TASK_STRUCT_NORMAL_PRIO_OFF
#define TASK_STRUCT_NORMAL_PRIO_OFF 0x84ULL

#undef ASHMEM_FOPS_OFF
#define ASHMEM_FOPS_OFF 0x02010238ULL
#undef ANON_PIPE_BUF_OPS_OFF
#define ANON_PIPE_BUF_OPS_OFF 0x01e81e60ULL
#undef KMALLOC_CACHES_OFF
#define KMALLOC_CACHES_OFF 0x02067330ULL

#undef ASHMEM_FOPS
#define ASHMEM_FOPS (KIMAGE_TEXT_BASE + ASHMEM_FOPS_OFF)
#undef ANON_PIPE_BUF_OPS
#define ANON_PIPE_BUF_OPS (KIMAGE_TEXT_BASE + ANON_PIPE_BUF_OPS_OFF)
#undef KMALLOC_CACHES
#define KMALLOC_CACHES (KIMAGE_TEXT_BASE + KMALLOC_CACHES_OFF)

#endif

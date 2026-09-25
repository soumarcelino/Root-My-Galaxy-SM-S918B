#ifndef OSS_CLONE_P0_TARGET_ZZHL_H
#define OSS_CLONE_P0_TARGET_ZZHL_H

/* Keep P0 route isolated from the legacy clone.  Relative include keeps this
 * import reproducible within Root-My-Galaxy-SM-S918B. */
#include "../../../payload/src/targets/dm3q-S918BXXUAZZHL/target.h"

/* Target profile was audited against ZZHL BTF/kallsyms.  The upstream profile
 * predates this correction; never inherit its SELinux address. */
#undef SELINUX_ENFORCING_OFF
#define SELINUX_ENFORCING_OFF 0x02d8e600ULL

/* Must match tools/run-zzhl-device.sh's staged helper path. */
#undef ROOT_UMH_PATH
#define ROOT_UMH_PATH "/data/local/tmp/oss-clone-zzhl/root-helper"

#endif

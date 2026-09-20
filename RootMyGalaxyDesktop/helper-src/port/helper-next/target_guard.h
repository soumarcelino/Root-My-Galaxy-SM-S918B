#ifndef RMG_TARGET_GUARD_H
#define RMG_TARGET_GUARD_H

#include <stdio.h>

#ifndef TARGET_GUARD_HEADER
#define TARGET_GUARD_HEADER "target-afzg1.h"
#endif
#include TARGET_GUARD_HEADER

/*
 * Validate every immutable property used to select kernel offsets.  A caller
 * must stop before touching the kernel when this function returns false.
 */
int rmg_target_validate(FILE *stream, int verbose);
int rmg_experimental_opt_in(FILE *stream);

#endif

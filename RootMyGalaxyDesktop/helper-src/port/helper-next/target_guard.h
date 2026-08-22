#ifndef RMG_TARGET_GUARD_H
#define RMG_TARGET_GUARD_H

#include <stdio.h>

#include "target-afzg1.h"

/*
 * Validate every immutable property used to select kernel offsets.  A caller
 * must stop before touching the kernel when this function returns false.
 */
int rmg_target_validate(FILE *stream, int verbose);
int rmg_experimental_opt_in(FILE *stream);

#endif

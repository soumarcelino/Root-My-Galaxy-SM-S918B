/*
 * Declares construction, handler installation, and bounded delivery of the
 * FPSIMD signal-frame payload.
 */
#ifndef OSS_CLONE_SIGNAL_FRAME_PAYLOAD_H
#define OSS_CLONE_SIGNAL_FRAME_PAYLOAD_H

#include <stdint.h>

int sigusr1_install_handler(void);

void sigusr1_build_payload(uint64_t page_base, uint64_t ashmem_misc_fops_addr);

int sigusr1_fire_and_wait(void);

#endif

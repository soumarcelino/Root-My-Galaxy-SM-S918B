/* Deterministic binfmt/usermode-helper bootstrap. */
#ifndef BOPE_UMH_ROOT_H
#define BOPE_UMH_ROOT_H

#include <stdint.h>

/* Atomically stages and byte-verifies the short helper and invalid-binfmt
 * trigger, then proves its ENOEXEC behavior under the exact verified kernel
 * contract. No kernel state or global tracing state is mutated. */
int root_umh_preflight(const char *root_umh_path);

/* Runs only after pipe-backed physical R/W is proven. It temporarily fills
 * the firmware's empty static usermode-helper path, invokes the kernel's
 * native synchronous binfmt request, and restores the original bytes. */
int root_umh_install_fd(int fd, uint64_t kernel_base,
                        uint64_t memstart_addr, uint64_t kimage_voffset,
                        const char *root_umh_path);

int root_umh_install_fd_tracked(int fd, uint64_t kernel_base,
                                uint64_t memstart_addr,
                                uint64_t kimage_voffset,
                                const char *root_umh_path,
                                int32_t *kernel_state,
                                int32_t irreversible_state);

int root_umh_install(uint64_t kernel_base, uint64_t page_base,
                     const char *root_umh_path);

#endif

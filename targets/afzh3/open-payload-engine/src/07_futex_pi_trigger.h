/*
 * Declares PI-futex trigger variants, their callbacks, and the staged trigger
 * interface used by the orchestrator.
 */
#ifndef OSS_CLONE_FUTEX_PI_TRIGGER_H
#define OSS_CLONE_FUTEX_PI_TRIGGER_H

#include <stdint.h>

int run_futex_trigger(void);

typedef int (*futex_post_trigger_cb)(void *ctx);
int run_futex_trigger_cb(futex_post_trigger_cb post_trigger_cb, void *ctx);

int run_futex_trigger_full(uint64_t page_base, uint64_t ashmem_misc_fops_addr,
                            futex_post_trigger_cb post_trigger_cb, void *ctx);

int run_futex_trigger_success_cb(futex_post_trigger_cb post_trigger_cb,
                                  void *ctx);
int run_futex_trigger_success_full(uint64_t page_base,
                                    uint64_t ashmem_misc_fops_addr,
                                    futex_post_trigger_cb post_trigger_cb,
                                    void *ctx);

int run_futex_trigger_v3_cb(futex_post_trigger_cb post_trigger_cb, void *ctx);
int run_futex_trigger_v3_full(uint64_t page_base,
                               uint64_t ashmem_misc_fops_addr,
                               futex_post_trigger_cb post_trigger_cb,
                               void *ctx);

int run_futex_trigger_v4_cb(futex_post_trigger_cb post_trigger_cb, void *ctx);
int run_futex_trigger_v4_full(uint64_t page_base,
                               uint64_t ashmem_misc_fops_addr,
                               futex_post_trigger_cb post_trigger_cb,
                               void *ctx);

int run_futex_trigger_v5_cb(futex_post_trigger_cb post_trigger_cb, void *ctx);
int run_futex_trigger_v5_full(uint64_t page_base,
                               uint64_t ashmem_misc_fops_addr,
                               futex_post_trigger_cb post_trigger_cb,
                               void *ctx);

int run_futex_trigger_v6_cb(futex_post_trigger_cb post_trigger_cb, void *ctx);
int run_futex_trigger_v6_full(uint64_t page_base,
                               uint64_t ashmem_misc_fops_addr,
                               futex_post_trigger_cb post_trigger_cb,
                               void *ctx);

int run_futex_trigger_v7_cb(futex_post_trigger_cb post_trigger_cb, void *ctx);
int run_futex_trigger_v7_full(uint64_t page_base,
                               uint64_t ashmem_misc_fops_addr,
                               futex_post_trigger_cb post_trigger_cb,
                               void *ctx);

int run_futex_trigger_v8_cb(futex_post_trigger_cb post_trigger_cb, void *ctx);
int run_futex_trigger_v8_full(uint64_t page_base,
                               uint64_t ashmem_misc_fops_addr,
                               futex_post_trigger_cb post_trigger_cb,
                               void *ctx);

int run_futex_trigger_v9_cb(futex_post_trigger_cb post_trigger_cb, void *ctx);
int run_futex_trigger_v9_full(uint64_t page_base,
                               uint64_t ashmem_misc_fops_addr,
                               futex_post_trigger_cb post_trigger_cb,
                               void *ctx);

int run_futex_trigger_v10_cb(futex_post_trigger_cb post_trigger_cb,
                              void *ctx);
int run_futex_trigger_v10_full(uint64_t page_base,
                                uint64_t ashmem_misc_fops_addr,
                                futex_post_trigger_cb post_trigger_cb,
                                void *ctx);

int run_futex_trigger_v11_cb(futex_post_trigger_cb post_trigger_cb,
                              void *ctx);
int run_futex_trigger_v11_full(uint64_t page_base,
                                uint64_t ashmem_misc_fops_addr,
                                futex_post_trigger_cb post_trigger_cb,
                                void *ctx);

int run_futex_trigger_v12_cb(futex_post_trigger_cb post_trigger_cb,
                              void *ctx);
int run_futex_trigger_v12_full(uint64_t page_base,
                                uint64_t ashmem_misc_fops_addr,
                                futex_post_trigger_cb post_trigger_cb,
                                void *ctx);

int run_futex_trigger_v13_cb(futex_post_trigger_cb post_trigger_cb,
                              void *ctx);
int run_futex_trigger_v13_full(uint64_t page_base,
                                uint64_t ashmem_misc_fops_addr,
                                futex_post_trigger_cb post_trigger_cb,
                                void *ctx);

int run_futex_trigger_v14_cb(futex_post_trigger_cb post_trigger_cb,
                              void *ctx);
int run_futex_trigger_v14_full(uint64_t page_base,
                                uint64_t ashmem_misc_fops_addr,
                                futex_post_trigger_cb post_trigger_cb,
                                void *ctx);
/* Debug instrumentation (H0): prints a timed marker relative to the v14
 * handshake entry. Safe to call from the waiter callback. */
void futex_v14_dbg(const char *name);

int run_futex_trigger_v14_full_staged(
    uint64_t page_base, uint64_t ashmem_misc_fops_addr,
    int32_t *mutation_state, int32_t pending_state, int32_t mutated_state,
    futex_post_trigger_cb post_trigger_cb, void *ctx);

#endif

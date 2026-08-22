#include "include/payload.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

enum {
    SIMULATION_ERROR_BASE = 80,
};

static int simulated_stage_failure(const struct payload_config *config,
                                   struct payload_shared_state *shared,
                                   unsigned stage)
{
    if (config->simulated_fail_stage != stage)
        return 0;
    shared->simulation_error_stage = stage;
    fprintf(stderr, "reconstructed payload: simulated stage %u failed\n",
            stage);
    return SIMULATION_ERROR_BASE + (int)stage;
}

static uint64_t simulation_token(uint64_t kernel_base, unsigned attempt)
{
    uint64_t value = kernel_base ^ ((uint64_t)attempt << 32) ^
                     UINT64_C(0x53494d554c415445);
    value ^= value >> 30;
    value *= UINT64_C(0xbf58476d1ce4e5b9);
    value ^= value >> 27;
    value *= UINT64_C(0x94d049bb133111eb);
    value ^= value >> 31;
    return value != 0 ? value : UINT64_C(1);
}

static int simulate_lifetime_race(const struct payload_config *config,
                                  struct payload_shared_state *shared,
                                  uint64_t kernel_base, unsigned attempt)
{
    int result;

    puts("\033[33m[*] \033[0mstage=probabilistic-lifetime-race (simulated)");
    result = simulated_stage_failure(config, shared, 8);
    if (result != 0)
        return result;
    if (shared->phase != PAYLOAD_PHASE_KERNEL_LOCATION_READY ||
        kernel_base == 0 || (kernel_base & UINT64_C(0xfff)) != 0)
        return EPROTO;
    shared->simulation_token = simulation_token(kernel_base, attempt);
    shared->simulation_flags |= PAYLOAD_SIM_LIFETIME_RACE;
    shared->phase = PAYLOAD_PHASE_LIFETIME_RACE_MODELED;
    return 0;
}

static int simulate_kernel_access(const struct payload_config *config,
                                  struct payload_shared_state *shared,
                                  uint64_t kernel_base, unsigned attempt)
{
    int result;

    puts("\033[33m[*] \033[0mstage=verifying-kernel-access (simulated)");
    result = simulated_stage_failure(config, shared, 9);
    if (result != 0)
        return result;
    if (shared->phase != PAYLOAD_PHASE_LIFETIME_RACE_MODELED ||
        (shared->simulation_flags & PAYLOAD_SIM_LIFETIME_RACE) == 0 ||
        shared->simulation_token != simulation_token(kernel_base, attempt))
        return EPROTO;
    shared->simulation_flags |= PAYLOAD_SIM_ACCESS_VERIFIED;
    shared->phase = PAYLOAD_PHASE_KERNEL_ACCESS_MODELED;
    return 0;
}

static int simulate_target_transition(const struct payload_config *config,
                                      struct payload_shared_state *shared)
{
    int result;

    puts("\033[33m[*] \033[0mstage=resolving-target-transition (simulated)");
    result = simulated_stage_failure(config, shared, 10);
    if (result != 0)
        return result;
    if (shared->phase != PAYLOAD_PHASE_KERNEL_ACCESS_MODELED ||
        (shared->simulation_flags & PAYLOAD_SIM_ACCESS_VERIFIED) == 0)
        return EPROTO;
    shared->simulation_target_id =
        shared->simulation_token ^ UINT64_C(0x504f435441524745);
    if (shared->simulation_target_id == 0)
        shared->simulation_target_id = UINT64_C(1);
    shared->simulation_flags |= PAYLOAD_SIM_TARGET_RESOLVED;
    shared->phase = PAYLOAD_PHASE_TARGET_TRANSITION_MODELED;
    puts("\033[33m[*] \033[0mstage=target-transition-ready (simulated)");
    return 0;
}

static int simulate_umh_bootstrap(const struct payload_config *config,
                                  struct payload_shared_state *shared)
{
    const char *helper = getenv("CVE43499_ROOT_HELPER");
    int result;

    puts("\033[33m[*] \033[0mstage=starting-temporary-root (simulated)");
    result = simulated_stage_failure(config, shared, 11);
    if (result != 0)
        return result;
    if (shared->phase != PAYLOAD_PHASE_TARGET_TRANSITION_MODELED ||
        (shared->simulation_flags & PAYLOAD_SIM_TARGET_RESOLVED) == 0 ||
        helper == NULL || helper[0] != '/')
        return EPROTO;
    shared->simulation_flags |= PAYLOAD_SIM_UMH_BOOTSTRAPPED;
    shared->phase = PAYLOAD_PHASE_UMH_BOOTSTRAP_MODELED;
    puts("\033[33m[*] \033[0mstage=temporary-root-ready (simulated)");
    shared->phase = PAYLOAD_PHASE_SIMULATION_COMPLETE;
    return 0;
}

int payload_single_attempt(const struct payload_config *config,
                           struct payload_shared_state *shared,
                           unsigned attempt)
{
    uint64_t kernel_base = 0;
    int result;

    printf("\033[33m[*] \033[0mexploit attempt=%u/%u\n", attempt,
           config->attempts);
    puts("\033[33m[*] \033[0mstage=preparing-kernel-access");
    puts("\033[33m[*] \033[0mstage=locating-kernel");
    if (payload_discover_kernel_base(config, &kernel_base) != 0)
        return 2;
    shared->simulation_flags = 0;
    shared->simulation_error_stage = 0;
    shared->simulation_token = 0;
    shared->simulation_target_id = 0;
    shared->phase = PAYLOAD_PHASE_KERNEL_LOCATION_READY;
    puts("\033[33m[*] \033[0mstage=kernel-location-ready");
    if (config->slide_only) {
        printf("\033[32m[+] \033[0mexploit completed attempt=%u/%u\n",
               attempt, config->attempts);
        return 0;
    }

    /* Stages 08-11 are a userspace-only model. They do not invoke exploit
     * primitives, access kernel memory, alter credentials, or spawn a helper. */
    result = simulate_lifetime_race(config, shared, kernel_base, attempt);
    if (result != 0)
        return result;
    result = simulate_kernel_access(config, shared, kernel_base, attempt);
    if (result != 0)
        return result;
    result = simulate_target_transition(config, shared);
    if (result != 0)
        return result;
    result = simulate_umh_bootstrap(config, shared);
    if (result != 0)
        return result;
    puts("\033[33m[*] \033[0mstage=finalizing-root-flow (simulated)");
    printf("\033[32m[+] \033[0mpoc completed attempt=%u/%u\n",
           attempt, config->attempts);
    return 0;
}

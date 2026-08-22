#define _GNU_SOURCE
#include "include/payload.h"

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static const uint64_t test_kernel_base = UINT64_C(0xffffffc008000000);

int payload_discover_kernel_base(const struct payload_config *config,
                                 uint64_t *kernel_base)
{
    (void)config;
    *kernel_base = test_kernel_base;
    return 0;
}

static struct payload_config test_config(void)
{
    struct payload_config config;

    memset(&config, 0, sizeof(config));
    config.attempts = 1;
    config.attempt_timeout_sec = 5;
    return config;
}

int main(void)
{
    struct payload_config config = test_config();
    struct payload_shared_state shared;
    unsigned stage;

    assert(setenv("CVE43499_ROOT_HELPER", "/simulated/ksu-helper", 1) == 0);
    memset(&shared, 0xa5, sizeof(shared));
    assert(payload_single_attempt(&config, &shared, 1) == 0);
    assert(shared.phase == PAYLOAD_PHASE_SIMULATION_COMPLETE);
    assert(shared.simulation_flags ==
           (PAYLOAD_SIM_LIFETIME_RACE | PAYLOAD_SIM_ACCESS_VERIFIED |
            PAYLOAD_SIM_TARGET_RESOLVED | PAYLOAD_SIM_UMH_BOOTSTRAPPED));
    assert(shared.simulation_error_stage == 0);
    assert(shared.simulation_token != 0);
    assert(shared.simulation_target_id != 0);

    config = test_config();
    config.slide_only = true;
    memset(&shared, 0, sizeof(shared));
    assert(payload_single_attempt(&config, &shared, 1) == 0);
    assert(shared.phase == PAYLOAD_PHASE_KERNEL_LOCATION_READY);
    assert(shared.simulation_flags == 0);

    config = test_config();
    assert(unsetenv("CVE43499_ROOT_HELPER") == 0);
    memset(&shared, 0, sizeof(shared));
    assert(payload_single_attempt(&config, &shared, 1) == EPROTO);
    assert(shared.phase == PAYLOAD_PHASE_TARGET_TRANSITION_MODELED);
    assert(setenv("CVE43499_ROOT_HELPER", "/simulated/ksu-helper", 1) == 0);

    for (stage = 8; stage <= 11; ++stage) {
        config = test_config();
        config.simulated_fail_stage = stage;
        memset(&shared, 0, sizeof(shared));
        assert(payload_single_attempt(&config, &shared, 1) == 80 + (int)stage);
        assert(shared.simulation_error_stage == stage);
        assert(shared.phase < PAYLOAD_PHASE_SIMULATION_COMPLETE);
    }
    return 0;
}

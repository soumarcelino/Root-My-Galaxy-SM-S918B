#ifndef CVE43499_PAYLOAD_H
#define CVE43499_PAYLOAD_H

#include <stdbool.h>
#include <stdint.h>

struct payload_config {
    unsigned attempts;
    unsigned pselect_delay_usec;
    unsigned attempt_timeout_sec;
    unsigned p0_timeout_sec;
    unsigned tracefs_sample_sec;
    unsigned simulated_fail_stage;
    bool slide_only;
};

enum payload_phase {
    PAYLOAD_PHASE_INITIAL = 0,
    PAYLOAD_PHASE_KERNEL_LOCATION_READY = 1,
    PAYLOAD_PHASE_LIFETIME_RACE_MODELED = 2,
    PAYLOAD_PHASE_KERNEL_ACCESS_MODELED = 3,
    PAYLOAD_PHASE_TARGET_TRANSITION_MODELED = 4,
    PAYLOAD_PHASE_UMH_BOOTSTRAP_MODELED = 5,
    PAYLOAD_PHASE_SIMULATION_COMPLETE = 6,
};

enum payload_simulation_flag {
    PAYLOAD_SIM_LIFETIME_RACE = 1U << 0,
    PAYLOAD_SIM_ACCESS_VERIFIED = 1U << 1,
    PAYLOAD_SIM_TARGET_RESOLVED = 1U << 2,
    PAYLOAD_SIM_UMH_BOOTSTRAPPED = 1U << 3,
};

struct payload_shared_state {
    uint32_t phase;
    uint32_t p0_ready;
    uint64_t p0_offset;
    uint64_t gate_page_struct;
    uint64_t probe_page_struct;
    uint32_t simulation_flags;
    uint32_t simulation_error_stage;
    uint64_t simulation_token;
    uint64_t simulation_target_id;
};

int payload_config_load(struct payload_config *config);
int payload_discover_kernel_base(const struct payload_config *config,
                                 uint64_t *kernel_base);
int payload_single_attempt(const struct payload_config *config,
                           struct payload_shared_state *shared,
                           unsigned attempt);

#endif

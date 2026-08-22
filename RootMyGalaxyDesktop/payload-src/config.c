#define _GNU_SOURCE
#include "include/payload.h"

#include <errno.h>
#include <limits.h>
#include <stdlib.h>

static int bounded_env(const char *name, unsigned fallback, unsigned minimum,
                       unsigned maximum, unsigned *output)
{
    const char *text = getenv(name);
    char *end = NULL;
    unsigned long value;

    if (text == NULL || *text == '\0') {
        *output = fallback;
        return 0;
    }
    errno = 0;
    value = strtoul(text, &end, 0);
    if (errno != 0 || end == text || *end != '\0' || value > UINT_MAX ||
        value < minimum || value > maximum)
        return -1;
    *output = (unsigned)value;
    return 0;
}

int payload_config_load(struct payload_config *config)
{
    if (bounded_env("EXPLOIT_ATTEMPTS", 8, 1, 64, &config->attempts) != 0 ||
        bounded_env("PSELECT_DELAY_USEC", 20000, 0, 1000000,
                    &config->pselect_delay_usec) != 0 ||
        bounded_env("EXPLOIT_ATTEMPT_TIMEOUT_SEC", 90, 5, 900,
                    &config->attempt_timeout_sec) != 0 ||
        bounded_env("P0_ATTEMPT_TIMEOUT_SEC", 20, 5,
                    config->attempt_timeout_sec, &config->p0_timeout_sec) != 0 ||
        bounded_env("TRACEFS_SAMPLE_SECONDS", 1, 1, 30,
                    &config->tracefs_sample_sec) != 0 ||
        bounded_env("SIMULATED_FAIL_STAGE", 0, 0, 11,
                    &config->simulated_fail_stage) != 0)
        return -1;
    config->slide_only = getenv("SLIDE_ONLY") != NULL;
    if (config->slide_only)
        config->attempts = 1;
    return 0;
}

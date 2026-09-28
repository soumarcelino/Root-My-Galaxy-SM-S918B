#define _GNU_SOURCE
#include <errno.h>
#include <sched.h>
#include <stdio.h>
#include <string.h>

#include "00_cpu_discovery.h"

struct core_ctl_cpu_state {
  unsigned char known;
  unsigned char paused;
  unsigned char not_preferred;
};

static int read_cpu_value(int cpu, const char *name, long *value) {
  char path[160];
  int length = snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%d/%s",
                        cpu, name);
  if (length <= 0 || (size_t)length >= sizeof(path)) return 0;
  FILE *file = fopen(path, "re");
  if (!file) return 0;
  int ok = fscanf(file, "%ld", value) == 1;
  fclose(file);
  return ok;
}

static int read_core_ctl_states(struct core_ctl_cpu_state *states,
                                size_t state_count) {
  FILE *file = NULL;
  char path[160];
  for (int cpu = 0; cpu < CPU_SETSIZE; cpu++) {
    int length = snprintf(path, sizeof(path),
                          "/sys/devices/system/cpu/cpu%d/core_ctl/global_state",
                          cpu);
    if (length <= 0 || (size_t)length >= sizeof(path)) continue;
    file = fopen(path, "re");
    if (file) break;
  }
  if (!file) return 0;

  int current_cpu = -1;
  unsigned char saw_paused[CPU_SETSIZE] = {0};
  unsigned char saw_not_preferred[CPU_SETSIZE] = {0};
  char line[160];
  while (fgets(line, sizeof(line), file)) {
    int value;
    if (sscanf(line, " CPU: %d", &value) == 1) {
      current_cpu = value >= 0 && (size_t)value < state_count ? value : -1;
      continue;
    }
    if (current_cpu < 0) continue;
    if (sscanf(line, " Paused: %d", &value) == 1) {
      states[current_cpu].paused = value != 0;
      saw_paused[current_cpu] = 1;
    } else if (sscanf(line, " Not preferred: %d", &value) == 1) {
      states[current_cpu].not_preferred = value != 0;
      saw_not_preferred[current_cpu] = 1;
    }
  }
  fclose(file);

  int known = 0;
  for (size_t cpu = 0; cpu < state_count; cpu++) {
    states[cpu].known = saw_paused[cpu] && saw_not_preferred[cpu];
    known += states[cpu].known != 0;
  }
  return known;
}

static void fill_result(int cpu, const struct core_ctl_cpu_state *state,
                        struct cpu_discovery_result *result) {
  memset(result, 0, sizeof(*result));
  result->cpu = cpu;
  read_cpu_value(cpu, "cpu_capacity", &result->capacity);
  read_cpu_value(cpu, "cpufreq/cpuinfo_max_freq",
                 &result->max_frequency_khz);
  result->core_ctl_known = state->known;
  result->paused = state->paused;
  result->not_preferred = state->not_preferred;
}

int cpu_discovery_select(struct cpu_discovery_result *result) {
  if (!result) {
    errno = EINVAL;
    return 0;
  }
  cpu_set_t allowed;
  if (sched_getaffinity(0, sizeof(allowed), &allowed) != 0) return 0;

  struct core_ctl_cpu_state states[CPU_SETSIZE] = {{0}};
  read_core_ctl_states(states, CPU_SETSIZE);

  int have_stable = 0;
  int have_capacity = 0;
  for (int cpu = 0; cpu < CPU_SETSIZE; cpu++) {
    if (!CPU_ISSET(cpu, &allowed)) continue;
    if (!states[cpu].known ||
        (!states[cpu].paused && !states[cpu].not_preferred)) {
      have_stable = 1;
    }
    long capacity;
    if (read_cpu_value(cpu, "cpu_capacity", &capacity)) have_capacity = 1;
  }

  int best_cpu = -1;
  long best_capacity = -1;
  long best_frequency = -1;
  for (int cpu = 0; cpu < CPU_SETSIZE; cpu++) {
    if (!CPU_ISSET(cpu, &allowed)) continue;
    int stable = !states[cpu].known ||
                 (!states[cpu].paused && !states[cpu].not_preferred);
    if (have_stable && !stable) continue;
    long capacity = -1;
    long frequency = -1;
    read_cpu_value(cpu, "cpu_capacity", &capacity);
    read_cpu_value(cpu, "cpufreq/cpuinfo_max_freq", &frequency);
    long metric = have_capacity ? capacity : frequency;
    long best_metric = have_capacity ? best_capacity : best_frequency;
    if (best_cpu < 0 || metric > best_metric ||
        (metric == best_metric && frequency > best_frequency) ||
        (metric == best_metric && frequency == best_frequency &&
         cpu < best_cpu)) {
      best_cpu = cpu;
      best_capacity = capacity;
      best_frequency = frequency;
    }
  }
  if (best_cpu < 0) {
    errno = ENODEV;
    return 0;
  }
  fill_result(best_cpu, &states[best_cpu], result);
  return 1;
}

int cpu_discovery_pin(int cpu) {
  if (cpu < 0 || cpu >= CPU_SETSIZE) {
    errno = EINVAL;
    return 0;
  }
  cpu_set_t set;
  CPU_ZERO(&set);
  CPU_SET(cpu, &set);
  return sched_setaffinity(0, sizeof(set), &set) == 0;
}

int cpu_discovery_pin_and_validate(int cpu,
                                   struct cpu_discovery_result *result) {
  if (!cpu_discovery_pin(cpu)) return 0;
  if (sched_getcpu() != cpu) {
    errno = EXDEV;
    return 0;
  }

  struct core_ctl_cpu_state states[CPU_SETSIZE] = {{0}};
  read_core_ctl_states(states, CPU_SETSIZE);
  struct cpu_discovery_result local;
  fill_result(cpu, &states[cpu], &local);
  if (result) *result = local;
  if (local.core_ctl_known && (local.paused || local.not_preferred)) {
    errno = EAGAIN;
    return 0;
  }
  return 1;
}

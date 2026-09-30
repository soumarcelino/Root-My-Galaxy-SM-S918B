#ifndef OSS_CPU_DISCOVERY_H
#define OSS_CPU_DISCOVERY_H

struct cpu_discovery_result {
  int cpu;
  long capacity;
  long max_frequency_khz;
  int core_ctl_known;
  int paused;
  int not_preferred;
};

int cpu_discovery_select(struct cpu_discovery_result *result);
int cpu_discovery_pin(int cpu);
int cpu_discovery_pin_and_validate(int cpu,
                                   struct cpu_discovery_result *result);

#endif

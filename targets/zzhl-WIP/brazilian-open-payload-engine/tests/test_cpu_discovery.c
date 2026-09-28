#include <stdio.h>

#include "00_cpu_discovery.h"

int main(void) {
  struct cpu_discovery_result result;
  if (!cpu_discovery_select(&result)) {
    perror("cpu_discovery_select");
    return 1;
  }
  if (!cpu_discovery_pin_and_validate(result.cpu, &result)) {
    perror("cpu_discovery_pin_and_validate");
    return 1;
  }
  printf("cpu=%d capacity=%ld max_freq_khz=%ld core_ctl_known=%d paused=%d "
         "not_preferred=%d\n",
         result.cpu, result.capacity, result.max_frequency_khz,
         result.core_ctl_known, result.paused, result.not_preferred);
  return 0;
}

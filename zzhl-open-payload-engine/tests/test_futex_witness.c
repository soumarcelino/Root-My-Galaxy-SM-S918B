#include <stdio.h>
#include "futex_witness.h"
int main(void) {
  const char text[] = "rmg_futex_witness: task=0xffffff886b628000 "
      "waiter=18446743799819090744 lock=18446743563445574288 "
      "waiter_task=18446743560115093504 task_pid=30330\n";
  struct oss_futex_witness w;
  if (!oss_parse_futex_witness_text(text, 30330, &w) ||
      w.task != 0xffffff886b628000ULL ||
      w.waiter != 0xffffffc03adb3b38ULL ||
      w.lock != 0xffffff8931e59a90ULL || w.pid != 30330) return 1;
  return 0;
}

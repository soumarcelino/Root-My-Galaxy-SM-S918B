#include "futex_witness.h"
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int kernel_ptr(uint64_t v) {
  return v >= 0xffffff8000000000ULL && v < 0xffffffffffffffffULL;
}

int oss_parse_futex_witness_text(const char *text, int expected_pid,
                                 struct oss_futex_witness *out) {
  if (!text || !out || expected_pid <= 0) return 0;
  struct oss_futex_witness last = {0};
  const char *line = text;
  while ((line = strstr(line, "rmg_futex_witness:")) != NULL) {
    const char *ts = strstr(line, " task=");
    const char *ws = strstr(line, " waiter=");
    const char *ls = strstr(line, " lock=");
    const char *wts = strstr(line, " waiter_task=");
    const char *ps = strstr(line, " task_pid=");
    if (ts && ws && ls && wts && ps) {
      errno = 0;
      uint64_t task = strtoull(ts + 6, NULL, 0);
      uint64_t waiter = strtoull(ws + 8, NULL, 0);
      uint64_t lock = strtoull(ls + 6, NULL, 0);
      uint64_t waiter_task = strtoull(wts + 13, NULL, 0);
      int pid = (int)strtol(ps + 10, NULL, 0);
      if (!errno && kernel_ptr(task) && kernel_ptr(waiter) &&
          kernel_ptr(lock) && waiter_task == task && pid == expected_pid)
        last = (struct oss_futex_witness){task, waiter, lock, pid};
    }
    line++;
  }
  if (!last.task) return 0;
  *out = last;
  return 1;
}

int oss_read_futex_witness_trace(int expected_pid,
                                 struct oss_futex_witness *out) {
  int fd = open("/sys/kernel/tracing/trace", O_RDONLY | O_CLOEXEC);
  if (fd < 0 || !out) return 0;
  size_t cap = 1024 * 1024, used = 0;
  char *buf = malloc(cap + 1);
  if (!buf) { close(fd); return 0; }
  while (used < cap) {
    ssize_t n = read(fd, buf + used, cap - used);
    if (n < 0 && errno == EINTR) continue;
    if (n <= 0) break;
    used += (size_t)n;
  }
  close(fd);
  buf[used] = 0;
  int ok = oss_parse_futex_witness_text(buf, expected_pid, out);
  free(buf);
  return ok;
}

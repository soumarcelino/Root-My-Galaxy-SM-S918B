/*
 * Appends optional boot-tagged checkpoints with a monotonic timestamp to a
 * durable diagnostic file.
 */
#ifndef OSS_DIAGNOSTIC_CHECKPOINT_H
#define OSS_DIAGNOSTIC_CHECKPOINT_H

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* Opt-in durable checkpoints for a diagnostic campaign. Keep writes outside
 * the individual workqueue mutations so the journal does not widen their
 * race window. Each line carries its boot ID to distinguish clean boots. */
static inline void oss_diag_checkpoint(const char *stage) {
  const char *enabled = getenv("RMG_TRACE_FILE");
  if (!enabled || strcmp(enabled, "1") != 0) {
    return;
  }
  char boot_id[48] = "unknown";
  int boot_fd = open("/proc/sys/kernel/random/boot_id", O_RDONLY | O_CLOEXEC);
  if (boot_fd >= 0) {
    ssize_t n = read(boot_fd, boot_id, sizeof(boot_id) - 1);
    close(boot_fd);
    if (n > 0) {
      boot_id[n] = '\0';
      boot_id[strcspn(boot_id, "\r\n")] = '\0';
    }
  }
  struct timespec now = {0};
  clock_gettime(CLOCK_BOOTTIME, &now);
  char line[160];
  int len = snprintf(line, sizeof(line), "%s %lld.%03ld %s\n", boot_id,
                     (long long)now.tv_sec, now.tv_nsec / 1000000, stage);
  if (len <= 0 || len >= (int)sizeof(line)) {
    return;
  }
  int fd = open("/data/local/tmp/rmg-trace.txt",
                O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0600);
  if (fd >= 0) {
    if (write(fd, line, (size_t)len) == len) {
      fsync(fd);
    }
    close(fd);
  }
}

#endif

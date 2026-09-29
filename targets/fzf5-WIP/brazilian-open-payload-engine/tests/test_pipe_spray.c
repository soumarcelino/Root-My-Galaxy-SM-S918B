/* Standalone test for pipe_spray.c -- ONLY real pipe()/fcntl() calls,
 * no fake kernel objects, no futex/rt_mutex, no ashmem. Validates
 * whether this device can actually create+resize the 480-pipe spray
 * FUN_00107dd4 uses (real prerequisite for eventually porting that
 * function), independent of and safe to run before any of the
 * corruption-related test harnesses. Also exercises the
 * RLIMIT_NOFILE/RLIMIT_MEMLOCK raise from groom.c, copied here
 * standalone since this test doesn't link groom.c. */
#include <stdio.h>
#include <stdlib.h>
#include <sys/resource.h>

#include "pipe_spray.h"

#define OSS_SPRAY_COUNT 480 /* matches FUN_00107dd4: 2x240 */

static void raise_rlimit_to_max_best_effort(int resource) {
  struct rlimit rl;
  if (getrlimit(resource, &rl) == 0) {
    rl.rlim_cur = rl.rlim_max;
    setrlimit(resource, &rl);
  }
}

int main(int argc, char **argv) {
  size_t count = OSS_SPRAY_COUNT;
  if (argc == 2) {
    count = (size_t)strtoul(argv[1], NULL, 10);
  }

  raise_rlimit_to_max_best_effort(RLIMIT_NOFILE);
  raise_rlimit_to_max_best_effort(RLIMIT_MEMLOCK);

  int *read_fds = calloc(count, sizeof(int));
  int *write_fds = calloc(count, sizeof(int));
  if (!read_fds || !write_fds) {
    fprintf(stderr, "calloc failed for count=%zu\n", count);
    return 1;
  }

  size_t created = pipe_spray_create(count, read_fds, write_fds);
  printf("created=%zu/%zu (2-page pipes)\n", created, count);
  if (created != count) {
    fprintf(stderr, "did not create all requested pipes\n");
  }

  size_t resized = pipe_spray_resize_all(count, read_fds, 32);
  printf("resized=%zu/%zu (to 32 pages)\n", resized, created);

  pipe_spray_close_all(count, read_fds, write_fds);
  printf("closed all\n");

  free(read_fds);
  free(write_fds);

  int ok = (created == count) && (resized == created);
  printf("result=%d\n", ok);
  return ok ? 0 : 1;
}

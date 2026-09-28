/* Isolated sanity check: does tgkill(SIGUSR2) from a control thread
 * interrupt a plain blocking syscall (nanosleep) in a target thread at
 * all, in this exact environment? If this fails too, the issue is
 * signal delivery mechanics in general, not something PI-futex
 * specific. */
#define _GNU_SOURCE
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

static atomic_int target_tid_g;

static void noop_handler(int sig) { (void)sig; }

static void *target_thread_fn(void *arg) {
  (void)arg;
  int tid = (int)syscall(SYS_gettid);
  atomic_store(&target_tid_g, tid);

  struct timespec req = {.tv_sec = 5, .tv_nsec = 0};
  struct timespec rem = {0};
  errno = 0;
  long ret = syscall(SYS_nanosleep, &req, &rem);
  int saved_errno = errno;
  fprintf(stderr, "[target] nanosleep ret=%ld errno=%d rem=%lds%ldns\n", ret,
          saved_errno, (long)rem.tv_sec, rem.tv_nsec);
  return NULL;
}

int main(void) {
  struct sigaction sa;
  __builtin_memset(&sa, 0, sizeof(sa));
  sa.sa_handler = noop_handler;
  if (sigaction(SIGUSR2, &sa, NULL) != 0) {
    fprintf(stderr, "sigaction failed errno=%d\n", errno);
    return 1;
  }

  pthread_t t;
  if (pthread_create(&t, NULL, target_thread_fn, NULL) != 0) {
    fprintf(stderr, "pthread_create failed\n");
    return 1;
  }

  int tid;
  for (;;) {
    tid = atomic_load(&target_tid_g);
    if (tid != 0) break;
    usleep(1000);
  }
  usleep(300000); /* let it genuinely enter the blocking syscall */

  long kret = syscall(SYS_tgkill, getpid(), tid, SIGUSR2);
  fprintf(stderr, "[main] tgkill(tid=%d) ret=%ld errno=%d\n", tid, kret,
          errno);

  pthread_join(t, NULL);
  fprintf(stderr, "[main] done\n");
  return 0;
}

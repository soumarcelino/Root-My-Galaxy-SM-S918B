/* Root-assisted, non-corrupting witness for the live PI futex objects.
 * A kprobe on rt_mutex_adjust_pi observes this program while one thread is
 * normally blocked on FUTEX_LOCK_PI. No signal-frame rewrite or exploit
 * payload is involved. */
#define _GNU_SOURCE

#include <errno.h>
#include <linux/futex.h>
#include <linux/sched.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>

static uint32_t g_pi_futex;
static atomic_int g_owner_ready;
static atomic_int g_waiter_started;
static atomic_int g_release_owner;
static atomic_int g_waiter_tid;

static long futex_call(uint32_t *uaddr, int op) {
  return syscall(SYS_futex, uaddr, op, 0, NULL, NULL, 0);
}

static void *owner_thread(void *unused) {
  (void)unused;
  if (futex_call(&g_pi_futex, FUTEX_LOCK_PI | FUTEX_PRIVATE_FLAG) != 0) {
    perror("owner FUTEX_LOCK_PI");
    return (void *)1;
  }
  atomic_store_explicit(&g_owner_ready, 1, memory_order_release);
  while (!atomic_load_explicit(&g_release_owner, memory_order_acquire))
    usleep(1000);
  if (futex_call(&g_pi_futex, FUTEX_UNLOCK_PI | FUTEX_PRIVATE_FLAG) != 0) {
    perror("owner FUTEX_UNLOCK_PI");
    return (void *)1;
  }
  return NULL;
}

static void *waiter_thread(void *unused) {
  (void)unused;
  atomic_store_explicit(&g_waiter_tid, (int)syscall(SYS_gettid),
                        memory_order_release);
  atomic_store_explicit(&g_waiter_started, 1, memory_order_release);
  if (futex_call(&g_pi_futex, FUTEX_LOCK_PI | FUTEX_PRIVATE_FLAG) != 0) {
    perror("waiter FUTEX_LOCK_PI");
    return (void *)1;
  }
  if (futex_call(&g_pi_futex, FUTEX_UNLOCK_PI | FUTEX_PRIVATE_FLAG) != 0) {
    perror("waiter FUTEX_UNLOCK_PI");
    return (void *)1;
  }
  return NULL;
}

int main(void) {
  pthread_t owner, waiter;
  if (pthread_create(&owner, NULL, owner_thread, NULL) != 0) return 1;
  while (!atomic_load_explicit(&g_owner_ready, memory_order_acquire))
    usleep(1000);
  if (pthread_create(&waiter, NULL, waiter_thread, NULL) != 0) return 1;
  while (!atomic_load_explicit(&g_waiter_started, memory_order_acquire))
    usleep(1000);

  /* Let FUTEX_LOCK_PI publish task->pi_blocked_on before sched_setattr. */
  usleep(100000);
  int tid = atomic_load_explicit(&g_waiter_tid, memory_order_acquire);
  struct sched_attr attr;
  memset(&attr, 0, sizeof(attr));
  attr.size = sizeof(attr);
  attr.sched_policy = SCHED_NORMAL;
  attr.sched_nice = 1;
  errno = 0;
  long ret = syscall(SYS_sched_setattr, tid, &attr, 0);
  int saved_errno = errno;
  printf("witness waiter_tid=%d sched_setattr=%ld errno=%d\n", tid, ret,
         saved_errno);

  atomic_store_explicit(&g_release_owner, 1, memory_order_release);
  void *owner_result = NULL, *waiter_result = NULL;
  pthread_join(owner, &owner_result);
  pthread_join(waiter, &waiter_result);
  return ret == 0 && owner_result == NULL && waiter_result == NULL ? 0 : 1;
}

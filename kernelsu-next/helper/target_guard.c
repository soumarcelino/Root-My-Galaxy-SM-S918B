#define _GNU_SOURCE

#include "target_guard.h"
#include "offset.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/system_properties.h>
#include <sys/utsname.h>
#include <unistd.h>

/* These are the BTF-derived values for S918BXXSAFZF5. */
_Static_assert(MM_STRUCT_SZ == 0x400, "mm_struct SLUB object size changed");
_Static_assert(FAKE_TASK_PRIO_OFF == 0x7c, "task_struct.prio changed");
_Static_assert(FAKE_TASK_NORMAL_PRIO_OFF == 0x84,
               "task_struct.normal_prio changed");
_Static_assert(FAKE_TASK_PI_LOCK_OFF == 0x884,
               "task_struct.pi_lock changed");
_Static_assert(FAKE_TASK_PI_WAITERS_OFF == 0x898,
               "task_struct.pi_waiters changed");
_Static_assert(FAKE_WAITER_TASK_OFF == 0x30,
               "rt_mutex_waiter.task changed");
_Static_assert(FAKE_WAITER_LOCK_OFF == 0x38,
               "rt_mutex_waiter.lock changed");
_Static_assert(FAKE_WAITER_PRIO_OFF == 0x44,
               "rt_mutex_waiter.prio changed");
_Static_assert(WQ_DFL_PWQ_OFF == 0xb0, "workqueue_struct.dfl_pwq changed");
_Static_assert(PWQ_NR_ACTIVE_OFF == 0x5c,
               "pool_workqueue.nr_active changed");
_Static_assert(POOL_WORKLIST_OFF == 0x20, "worker_pool.worklist changed");
_Static_assert(WORK_FUNC_OFF == 0x18, "work_struct.func changed");

struct property_expectation {
  const char *name;
  const char *expected;
};

static int check_property(FILE *stream,
                          const struct property_expectation *item,
                          int verbose) {
  char actual[PROP_VALUE_MAX] = {0};
  int length = __system_property_get(item->name, actual);
  int match = length > 0 && strcmp(actual, item->expected) == 0;

  if (!match || verbose) {
    fprintf(stream, "target %s property=%s actual=%s expected=%s\n",
            match ? "ok" : "mismatch", item->name,
            length > 0 ? actual : "<missing>", item->expected);
  }
  return match;
}

int rmg_target_validate(FILE *stream, int verbose) {
  if (!stream) {
    stream = stderr;
  }

  static const struct property_expectation properties[] = {
      {"ro.product.model", TARGET_MODEL},
      {"ro.product.device", TARGET_DEVICE},
      {"ro.build.display.id", TARGET_BUILD_DISPLAY},
      {"ro.build.fingerprint", BUILD_FINGERPRINT},
      {"ro.build.version.sdk", TARGET_ANDROID_SDK},
      {"ro.product.cpu.abi", TARGET_ABI},
  };

  int valid = 1;
  for (size_t index = 0; index < sizeof(properties) / sizeof(properties[0]);
       index++) {
    valid &= check_property(stream, &properties[index], verbose);
  }

  struct utsname uts;
  if (uname(&uts) != 0) {
    fprintf(stream, "target mismatch uname errno=%d\n", errno);
    valid = 0;
  } else {
    int release_ok = strcmp(uts.release, TARGET_KERNEL_RELEASE) == 0;
    int version_ok = strcmp(uts.version, TARGET_KERNEL_VERSION) == 0;
    if (!release_ok || verbose) {
      fprintf(stream, "target %s kernel-release actual=%s expected=%s\n",
              release_ok ? "ok" : "mismatch", uts.release,
              TARGET_KERNEL_RELEASE);
    }
    if (!version_ok || verbose) {
      fprintf(stream, "target %s kernel-version actual=%s expected=%s\n",
              version_ok ? "ok" : "mismatch", uts.version,
              TARGET_KERNEL_VERSION);
    }
    valid &= release_ok && version_ok;
  }

  long page_size = sysconf(_SC_PAGESIZE);
  int page_ok = page_size == TARGET_PAGE_SIZE;
  if (!page_ok || verbose) {
    fprintf(stream, "target %s page-size actual=%ld expected=%d\n",
            page_ok ? "ok" : "mismatch", page_size, TARGET_PAGE_SIZE);
  }
  valid &= page_ok;

  if (!valid) {
    fprintf(stream,
            "target rejected: offsets are valid only for %s/%s (%s)\n",
            TARGET_MODEL, TARGET_DEVICE, TARGET_BUILD_DISPLAY);
  } else if (verbose) {
    fprintf(stream, "target accepted profile=%s\n", TARGET_PROFILE_ID);
  }
  return valid;
}

int rmg_experimental_opt_in(FILE *stream) {
  const char *value = getenv("RMG_OPEN_EXPERIMENTAL");
  int accepted = value && strcmp(value, TARGET_EXPERIMENTAL_TOKEN) == 0;
  if (!accepted) {
    if (!stream) {
      stream = stderr;
    }
    fprintf(stream,
            "open engine is not runtime-validated; set "
            "RMG_OPEN_EXPERIMENTAL=%s only for an explicitly supervised run\n",
            TARGET_EXPERIMENTAL_TOKEN);
  }
  return accepted;
}

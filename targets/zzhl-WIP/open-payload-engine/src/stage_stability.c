#define _GNU_SOURCE
#include "stage_stability.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int read_psi(const char *path, double *value) {
  FILE *f = fopen(path, "re");
  char line[256];
  int ok = 0;
  if (f && fgets(line, sizeof(line), f)) {
    char *p = strstr(line, "avg10=");
    if (p) {
      char *end;
      errno = 0;
      *value = strtod(p + 6, &end);
      ok = errno == 0 && end != p + 6;
    }
  }
  if (f) fclose(f);
  return ok;
}

static int sample(long *mem_kb, long *temp_mc, int *runnable,
                  double *cpu, double *mem, double *io) {
  FILE *f = fopen("/proc/meminfo", "re");
  char key[64], unit[16];
  long amount;
  int have_mem = 0;
  while (f && fscanf(f, "%63s %ld %15s", key, &amount, unit) == 3) {
    if (!strcmp(key, "MemAvailable:")) {
      *mem_kb = amount;
      have_mem = 1;
      break;
    }
  }
  if (f) fclose(f);
  f = fopen("/proc/loadavg", "re");
  double l1, l5, l15;
  int total;
  int have_load = f && fscanf(f, "%lf %lf %lf %d/%d", &l1, &l5, &l15,
                              runnable, &total) == 5;
  if (f) fclose(f);

  DIR *dir = opendir("/sys/class/thermal");
  struct dirent *entry;
  *temp_mc = 0;
  while (dir && (entry = readdir(dir))) {
    if (strncmp(entry->d_name, "thermal_zone", 12)) continue;
    char path[256];
    snprintf(path, sizeof(path), "/sys/class/thermal/%s/temp", entry->d_name);
    f = fopen(path, "re");
    long value = 0;
    if (f && fscanf(f, "%ld", &value) == 1 && value > *temp_mc &&
        value < 200000) *temp_mc = value;
    if (f) fclose(f);
  }
  if (dir) closedir(dir);
  return have_mem && have_load && *temp_mc > 0 &&
         read_psi("/proc/pressure/cpu", cpu) &&
         read_psi("/proc/pressure/memory", mem) &&
         read_psi("/proc/pressure/io", io);
}

static void sleep_interval(time_t seconds) {
  struct timespec remaining = {.tv_sec = seconds};
  while (nanosleep(&remaining, &remaining) != 0 && errno == EINTR) {
  }
}

int oss_stage_stability_gate(const char *stage) {
  int stable = 0;
  for (int elapsed = 0; elapsed <= 300 && stable < 3; elapsed += 3) {
    long mem_kb = 0, temp_mc = 0;
    int runnable = 0;
    double cpu = 0, mem = 0, io = 0;
    int valid = sample(&mem_kb, &temp_mc, &runnable, &cpu, &mem, &io);
    int pass = valid && mem_kb >= 1024L * 1024L && temp_mc <= 48000 &&
               runnable <= 8 && cpu <= 25.0 && mem <= 3.0 && io <= 5.0;
    stable = pass ? stable + 1 : 0;
    fprintf(stderr,
            "[stage-gate] stage=%s gate=%d/3 temp=%.1fC mem=%ldMB "
            "runnable=%d psi=%.2f/%.2f/%.2f\n",
            stage, stable, temp_mc / 1000.0, mem_kb / 1024, runnable,
            cpu, mem, io);
    if (stable == 3) return 1;
    if (elapsed == 300) break;
    sleep_interval(3);
  }
  fprintf(stderr, "[safe-stop] stage=%s stability gate timeout\n", stage);
  return 0;
}

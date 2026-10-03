#define _GNU_SOURCE
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/system_properties.h>
#include <sys/utsname.h>

#include "runtime_contract.h"

uint64_t rmg_runtime_values[RMG_RUNTIME_COUNT];

#define RMG_RUNTIME_KEY(name) [RMG_INDEX_##name] = #name,
static const char *const runtime_names[] = {
#include "runtime_keys.def"
};
#undef RMG_RUNTIME_KEY

static int read_property(const char *name, const char *expected) {
  char actual[PROP_VALUE_MAX];
  int n = __system_property_get(name, actual);
  return n > 0 && strcmp(actual, expected) == 0;
}

static int read_boot_id(const char *expected) {
  char actual[64];
  FILE *fp = fopen("/proc/sys/kernel/random/boot_id", "re");
  if (!fp) return 0;
  int ok = fgets(actual, sizeof(actual), fp) != NULL;
  fclose(fp);
  if (!ok) return 0;
  actual[strcspn(actual, "\r\n")] = '\0';
  return strcmp(actual, expected) == 0;
}

static int copy_unique(char *dest, size_t size, const char *value) {
  size_t len = strlen(value);
  if (dest[0] || !len || len >= size) return 0;
  memcpy(dest, value, len + 1);
  return 1;
}

int rmg_runtime_load(const char *path) {
  if (!path || !*path) return 0;
  FILE *fp = fopen(path, "re");
  if (!fp) return 0;
  struct stat st;
  if (fstat(fileno(fp), &st) != 0 || !S_ISREG(st.st_mode) ||
      st.st_size <= 0 || st.st_size > 32768) {
    fclose(fp);
    return 0;
  }

  uint64_t values[RMG_RUNTIME_COUNT] = {0};
  unsigned char seen[RMG_RUNTIME_COUNT] = {0};
  char model[64] = {0}, device[64] = {0}, build[96] = {0};
  char fingerprint[256] = {0}, release[128] = {0};
  char version[160] = {0}, boot_id[64] = {0}, schema[8] = {0};
  char line[512];
  int good = 1;
  while (fgets(line, sizeof(line), fp)) {
    size_t n = strlen(line);
    if (!n || line[n - 1] != '\n') { good = 0; break; }
    line[n - 1] = '\0';
    char *equals = strchr(line, '=');
    if (!equals || equals == line || strchr(equals + 1, '=')) { good = 0; break; }
    *equals++ = '\0';
    if (!strcmp(line, "schema")) good = copy_unique(schema, sizeof(schema), equals);
    else if (!strcmp(line, "model")) good = copy_unique(model, sizeof(model), equals);
    else if (!strcmp(line, "device")) good = copy_unique(device, sizeof(device), equals);
    else if (!strcmp(line, "build")) good = copy_unique(build, sizeof(build), equals);
    else if (!strcmp(line, "fingerprint")) good = copy_unique(fingerprint, sizeof(fingerprint), equals);
    else if (!strcmp(line, "kernel_release")) good = copy_unique(release, sizeof(release), equals);
    else if (!strcmp(line, "kernel_version")) good = copy_unique(version, sizeof(version), equals);
    else if (!strcmp(line, "boot_id")) good = copy_unique(boot_id, sizeof(boot_id), equals);
    else {
      int index = -1;
      for (int i = 0; i < RMG_RUNTIME_COUNT; i++) {
        if (!strcmp(line, runtime_names[i])) { index = i; break; }
      }
      if (index < 0 || seen[index]) { good = 0; break; }
      errno = 0;
      char *end;
      unsigned long long value = strtoull(equals, &end, 0);
      if (errno || end == equals || *end) { good = 0; break; }
      values[index] = (uint64_t)value;
      seen[index] = 1;
    }
    if (!good) break;
  }
  if (ferror(fp)) good = 0;
  fclose(fp);
  if (!good || strcmp(schema, "1") || !model[0] || !device[0] ||
      !build[0] || !fingerprint[0] || !release[0] || !version[0] ||
      !boot_id[0]) return 0;
  for (int i = 0; i < RMG_RUNTIME_COUNT; i++) if (!seen[i]) return 0;

  struct utsname uts;
  if (uname(&uts) != 0 || strcmp(uts.release, release) ||
      strcmp(uts.version, version) ||
      !read_property("ro.product.model", model) ||
      !read_property("ro.product.device", device) ||
      !read_property("ro.build.version.incremental", build) ||
      !read_property("ro.build.fingerprint", fingerprint) ||
      !read_boot_id(boot_id)) return 0;

  if (values[RMG_INDEX_TARGET_KIMAGE_TEXT_BASE] < 0xffffffc000000000ULL ||
      values[RMG_INDEX_TARGET_KIMAGE_TEXT_BASE] > 0xffffffc100000000ULL ||
      values[RMG_INDEX_TARGET_LINEAR_MAP_BASE] >=
          values[RMG_INDEX_TARGET_LINEAR_MAP_END] ||
      values[RMG_INDEX_TARGET_VMEMMAP_START] >=
          values[RMG_INDEX_TARGET_VMEMMAP_END]) return 0;

  memcpy(rmg_runtime_values, values, sizeof(values));
  return 1;
}

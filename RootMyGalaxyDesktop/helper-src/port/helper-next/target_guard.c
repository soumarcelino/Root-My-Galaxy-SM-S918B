#include "target_guard.h"

#include <stdlib.h>
#include <string.h>
#include <sys/system_properties.h>
#include <sys/utsname.h>

static int property_matches(FILE *stream, const char *name, const char *expected)
{
  char value[PROP_VALUE_MAX] = {0};

  if (__system_property_get(name, value) <= 0 || strcmp(value, expected) != 0) {
    fprintf(stream, "target mismatch %s expected=%s actual=%s\n",
            name, expected, value[0] ? value : "<missing>");
    return 0;
  }
  return 1;
}

int rmg_target_validate(FILE *stream, int verbose)
{
  struct utsname uts;
  int valid = 1;
  int uname_ok = uname(&uts) == 0;

  valid &= property_matches(stream, "ro.product.device", TARGET_DEVICE);
  valid &= property_matches(stream, "ro.build.display.id", TARGET_BUILD_DISPLAY);
  valid &= property_matches(stream, "ro.build.version.sdk", TARGET_ANDROID_SDK);
  if (!uname_ok || strcmp(uts.release, TARGET_KERNEL_RELEASE) != 0) {
    fprintf(stream, "target mismatch kernel expected=%s actual=%s\n",
            TARGET_KERNEL_RELEASE, uname_ok ? uts.release : "<unavailable>");
    valid = 0;
  }
  if (valid && verbose)
    fprintf(stream, "target profile=%s validated\n", TARGET_PROFILE_ID);
  return valid;
}

int rmg_experimental_opt_in(FILE *stream)
{
  const char *token = getenv("RMG_EXPERIMENTAL_TOKEN");
  if (token && strcmp(token, TARGET_EXPERIMENTAL_TOKEN) == 0)
    return 1;
  fprintf(stream, "experimental token missing or invalid\n");
  return 0;
}

#include "../../../../native/compact_log.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int capture_logs(char *output, size_t capacity) {
  FILE *capture = tmpfile();
  if (!capture) return 0;
  int saved_stderr = dup(STDERR_FILENO);
  if (saved_stderr < 0 || dup2(fileno(capture), STDERR_FILENO) < 0) {
    fclose(capture);
    return 0;
  }

  fprintf(stderr, "\x1b[33m[*] \x1b[0mstage=temporary-root-ready\n");
  fprintf(stderr, "[pipe_rw] telemetry stage_ms");
  fprintf(stderr, " locate=120ms reclaim=13ms verify=4ms total=137ms\n");
  fprintf(stderr, "[pipe_rw] selection page=7 candidate=19\n");
  fprintf(stderr, "[pipe_rw] terminal failure stage=proof\n");
  fprintf(stderr,
          "[launcher] gate=1/2 temp=37.0C mem=6800MB runnable=1 load=3.0\n");
  fflush(stderr);

  int restored = dup2(saved_stderr, STDERR_FILENO) >= 0;
  close(saved_stderr);
  rewind(capture);
  size_t used = fread(output, 1, capacity - 1U, capture);
  output[used] = '\0';
  fclose(capture);
  return restored;
}

int main(void) {
  char output[4096];
  if (!capture_logs(output, sizeof(output))) return 1;
  if (strchr(output, '\x1b') != NULL) return 2;
  if (strstr(output, "stage=temporary-root-ready") == NULL) return 3;
  if (strstr(output, "[pipe_rw] telemetry stage_ms") == NULL) return 4;
  if (strstr(output, "[pipe_rw] telemetry stage_ms locate=120ms reclaim=13ms "
                     "verify=4ms total=137ms\n") == NULL)
    return 9;
  if (strstr(output, "[launcher] gate=1/2 temp=37.0C") == NULL) return 5;
  if (strstr(output, "selection page=7") != NULL) return 6;
  if (strstr(output, "terminal failure") == NULL) return 7;

  const char *cursor = output;
  while (*cursor != '\0') {
    const char *newline = strchr(cursor, '\n');
    size_t length = newline ? (size_t)(newline - cursor) : strlen(cursor);
    if (length == 0U || cursor[0] != '[') return 8;
    if (!newline) break;
    cursor = newline + 1;
  }
  return 0;
}

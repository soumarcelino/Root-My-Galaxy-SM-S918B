#include <stdint.h>

#define SYS_READ 63
#define SYS_WRITE 64
#define SYS_EXIT 93
#define SYS_EXECVE 221
#define COMMAND_FD 198
#define READY_FD 199
#define READY_MAGIC 0x4d4d5244u

static long syscall1(long number, long arg0) {
  register long x0 __asm__("x0") = arg0;
  register long x8 __asm__("x8") = number;
  __asm__ volatile("svc 0" : "+r"(x0) : "r"(x8) : "memory", "cc");
  return x0;
}

static long syscall3(long number, long arg0, long arg1, long arg2) {
  register long x0 __asm__("x0") = arg0;
  register long x1 __asm__("x1") = arg1;
  register long x2 __asm__("x2") = arg2;
  register long x8 __asm__("x8") = number;
  __asm__ volatile("svc 0"
                   : "+r"(x0)
                   : "r"(x1), "r"(x2), "r"(x8)
                   : "memory", "cc");
  return x0;
}

static int write_ready(void) {
  uint32_t magic = READY_MAGIC;
  unsigned char *cursor = (unsigned char *)&magic;
  long remaining = sizeof(magic);
  while (remaining > 0) {
    long written = syscall3(SYS_WRITE, READY_FD, (long)cursor, remaining);
    if (written == -4) continue;
    if (written <= 0) return 0;
    cursor += written;
    remaining -= written;
  }
  return 1;
}

__attribute__((noreturn, visibility("default"))) void _start(void) {
  static char path[] = "/proc/self/exe";
  char *argv[] = {path, 0};
  char *envp[] = {0};
  unsigned char command;

  if (!write_ready()) syscall1(SYS_EXIT, 10);
  for (;;) {
    long count = syscall3(SYS_READ, COMMAND_FD, (long)&command, 1);
    if (count == -4) continue;
    if (count != 1 || command != 1) syscall1(SYS_EXIT, 11);
    syscall3(SYS_EXECVE, (long)path, (long)argv, (long)envp);
    syscall1(SYS_EXIT, 12);
  }
}

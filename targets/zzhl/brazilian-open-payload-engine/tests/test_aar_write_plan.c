/* Validate the real ConfigFS AAW encoder, including addresses rejected by the
 * old fixed-position plan. Pass --full to sweep every target vmemmap slot. */
#define _GNU_SOURCE
#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../src/08_ashmem_configfs_rw.c"

static uint64_t checked;
static uint64_t maximum_step;
static uint32_t maximum_slack;

static int control_survives_replay(const unsigned char *control) {
  unsigned char encoded[TARGET_CONFIGFS_CONTROL_LEN];
  simulate_ashmem_name_blob(control, sizeof(encoded), encoded);
  return memcmp(control + TARGET_CONFIGFS_WRITE_STATE_OFF,
                encoded + TARGET_CONFIGFS_WRITE_STATE_OFF,
                TARGET_CONFIGFS_WRITE_STATE_LEN) == 0;
}

static void check_plan(uint64_t address, size_t len) {
  struct configfs_write_plan plan;
  unsigned char control[TARGET_CONFIGFS_CONTROL_LEN];
  assert(prepare_configfs_write_control(address, len, &plan, control));

  uint64_t bin_buffer = 0;
  uint32_t size = 0;
  memcpy(&bin_buffer, control + TARGET_CONFIGFS_WRITE_HIGH_OFF,
         sizeof(bin_buffer));
  memcpy(&size, control + TARGET_CONFIGFS_WRITE_END_OFF, sizeof(size));
  assert(plan.pos <= INT32_MAX);
  assert(bin_buffer + plan.pos == address);
  assert(size == plan.size && size <= INT32_MAX);
  assert((uint64_t)size >= plan.pos + len);
  assert(control_survives_replay(control));

  uint64_t low = address & TARGET_CONFIGFS_ADDR_LOW_MASK;
  assert(plan.pos >= low);
  assert((plan.pos - low) % CONFIGFS_WRITE_POS_STEP == 0);
  uint64_t step = (plan.pos - low) / CONFIGFS_WRITE_POS_STEP;
  uint32_t slack = plan.size - (uint32_t)(plan.pos + len);
  if (step > maximum_step) maximum_step = step;
  if (slack > maximum_slack) maximum_slack = slack;
  checked++;
}

static void check_regressions(void) {
  static const uint64_t formerly_rejected_slots[] = {
      0xfffffffe00ff5018ULL,
      0xfffffffe00ba9218ULL,
      0xfffffffe00bc2818ULL,
      0xfffffffe00159418ULL,
      0xfffffffe0022c598ULL,
  };
  for (size_t i = 0;
       i < sizeof(formerly_rejected_slots) / sizeof(formerly_rejected_slots[0]);
       i++) {
    check_plan(formerly_rejected_slots[i], sizeof(uint64_t));
  }

  static const struct {
    uint64_t address;
    size_t len;
  } preserved[] = {
      {0xffffffc00ae96600ULL, 1},
      {0xffffff8a30372180ULL, 35},
      {0xffffff895399b000ULL, 40},
      {0xffffff885206b600ULL, 8},
      {0xffffff803fd40dc0ULL, 8},
      {0xfffffffe01000018ULL, 8},
      {0xfffffffe02000018ULL, 8},
  };
  for (size_t i = 0; i < sizeof(preserved) / sizeof(preserved[0]); i++) {
    struct configfs_write_plan plan;
    unsigned char control[TARGET_CONFIGFS_CONTROL_LEN];
    assert(prepare_configfs_write_control(preserved[i].address,
                                          preserved[i].len, &plan, control));
    assert(plan.pos ==
           (preserved[i].address & TARGET_CONFIGFS_ADDR_LOW_MASK));
    assert(plan.size == (uint32_t)(plan.pos + preserved[i].len));
    assert(control_survives_replay(control));
    checked++;
  }
}

static void check_coverage(int full) {
  uint64_t stride = full ? 1 : 977;
  uint64_t pfn_limit = (12ULL << 30) / TARGET_PAGE_SIZE;
  for (uint64_t pfn = 0; pfn < pfn_limit; pfn += stride) {
    uint64_t slot = TARGET_VMEMMAP_START +
                    pfn * TARGET_STRUCT_PAGE_SIZE +
                    TARGET_PAGE_SLAB_CACHE_OFF;
    if (slot >= TARGET_VMEMMAP_END) break;
    check_plan(slot, sizeof(uint64_t));
  }
  check_plan(TARGET_VMEMMAP_START + TARGET_PAGE_SLAB_CACHE_OFF,
             sizeof(uint64_t));
  check_plan(TARGET_VMEMMAP_END - TARGET_STRUCT_PAGE_SIZE +
                 TARGET_PAGE_SLAB_CACHE_OFF,
             sizeof(uint64_t));

  static const size_t lengths[] = {1, 2, 4, 8, 16, 23, 24, 32, 64, 128, 256};
  uint64_t seed = 0x243f6a8885a308d3ULL;
  long samples = full ? 200000 : 20000;
  for (long i = 0; i < samples; i++) {
    seed = seed * 6364136223846793005ULL + 1442695040888963407ULL;
    uint64_t bits = seed >> 8;
    uint64_t address;
    switch (seed % 3) {
    case 0:
      address = TARGET_LINEAR_MAP_BASE + (bits & 0x3fffffff0ULL);
      break;
    case 1:
      address = TARGET_VMEMMAP_START + (bits & 0x3ffffff0ULL);
      break;
    default:
      address = TARGET_LINEAR_MAP_BASE + (bits & 0xfff0ULL);
      break;
    }
    size_t len = lengths[(seed >> 40) %
                         (sizeof(lengths) / sizeof(lengths[0]))];
    if ((address & TARGET_PAGE_MASK) + len <= TARGET_PAGE_SIZE) {
      check_plan(address, len);
    }
  }
}

int main(int argc, char **argv) {
  int full = argc == 2 && strcmp(argv[1], "--full") == 0;
  assert(argc == 1 || full);
  check_regressions();
  check_coverage(full);
  printf("coverage: %llu addresses, max_position_step=%llu max_size_slack=%u\n",
         (unsigned long long)checked, (unsigned long long)maximum_step,
         maximum_slack);
  puts("PASS exact AAW destination with flexible ConfigFS position");
  return 0;
}

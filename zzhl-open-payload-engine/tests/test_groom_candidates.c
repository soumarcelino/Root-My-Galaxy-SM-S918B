#include <assert.h>
#include <stddef.h>
#include <stdint.h>

#include "groom.h"

struct source_state { size_t index; };

static uint64_t source(void *opaque) {
  static const uint64_t values[] = {
      0, 0xffffff8001000000ULL, 0xffffff8001000000ULL,
      0xffffff8001008000ULL, 0xffffff8001010000ULL,
      0xffffff8001018000ULL,
  };
  struct source_state *state = opaque;
  return state->index < sizeof(values) / sizeof(values[0])
             ? values[state->index++] : 0;
}

int main(void) {
  struct source_state state = {0};
  struct oss_fops_candidate candidates[OSS_FOPS_CANDIDATE_COUNT];
  assert(oss_collect_fops_candidates(source, &state, candidates) == 4);
  for (size_t i = 0; i < OSS_FOPS_CANDIDATE_COUNT; i++) {
    assert((candidates[i].page & 0xfff) == 0);
    assert(candidates[i].object == candidates[i].page + 0x180);
    if (i) assert(candidates[i - 1].page != candidates[i].page);
  }
  return 0;
}

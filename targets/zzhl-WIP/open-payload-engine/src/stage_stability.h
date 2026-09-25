#ifndef OSS_CLONE_STAGE_STABILITY_H
#define OSS_CLONE_STAGE_STABILITY_H

/* Three consecutive relaxed-profile samples. Returns zero on timeout/read
 * failure, so callers stop before entering a new allocator/mutation stage. */
int oss_stage_stability_gate(const char *stage);

#endif

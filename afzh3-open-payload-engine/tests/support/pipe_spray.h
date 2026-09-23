#ifndef OSS_CLONE_PIPE_SPRAY_H
#define OSS_CLONE_PIPE_SPRAY_H

#include <stddef.h>

/* source: FUN_00107dd4's opening loops (raw disasm, `asm.varsub=false`,
 * this session -- see docs/kernel-reference/README.md "FUN_00107dd4
 * fully re-traced with corrected methodology"). Creates `count` pipes,
 * each sized to 2 pages via F_SETPIPE_SZ, storing the fd pairs into
 * caller-provided arrays. Returns the number of pipes successfully
 * created (may be less than `count` on fd exhaustion -- caller should
 * check against `count` and call pipe_spray_close_all() to clean up
 * partial results either way). read_fds/write_fds must each have room
 * for `count` ints. This is ONLY the pipe pre-creation step -- does
 * NOT touch any kernel object beyond real, ordinary pipe() calls, safe
 * to test standalone on any device. */
size_t pipe_spray_create(size_t count, int *read_fds, int *write_fds);

/* source: FUN_00107dd4's post-leak resize loops -- fcntl(fd,
 * F_SETPIPE_SZ, pages<<12) on every already-open pipe from
 * pipe_spray_create(). `pages` was 32 in the closed binary (vs the
 * initial 2-page size). Operates on read_fds[i] (fds[0], the read end)
 * to match the exact fd the closed binary's FUN_00107d50 targets --
 * confirmed by disasm (both FUN_00107d18 at creation and FUN_00107d50
 * here dereference the SAME first-array-element pointer). F_SETPIPE_SZ
 * affects the underlying pipe regardless of which end it's called on,
 * so this choice likely doesn't matter functionally, but matching the
 * observed fd is not a guess either way. Returns the number of pipes
 * successfully resized. Still only real fcntl() calls on real pipes --
 * safe to test standalone. */
size_t pipe_spray_resize_all(size_t count, const int *read_fds, int pages);

/* Closes all fds in both arrays (skips already-closed / -1 entries).
 * Safe to call on a partially-populated array (e.g. after
 * pipe_spray_create() returns fewer than `count`). */
void pipe_spray_close_all(size_t count, int *read_fds, int *write_fds);

#endif

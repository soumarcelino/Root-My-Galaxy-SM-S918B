#ifndef OSS_CLONE_P0_ZZHL_SAFETY_H
#define OSS_CLONE_P0_ZZHL_SAFETY_H

/*
 * ZZHL P0 never reuses a discovery/reclaim session.  The former fallback
 * reached sched_setattr() with an unverified rb_node and panicked in
 * rb_erase().  These switches select the guarded upstream branch.
 */
#define APP_REQUIRE_FRESH_P0_SESSION 1
#define SLIDE_SYNC_PSELECT_SYSCALL 1
#define SLIDE_GUARD_PSELECT_SYSCALL 1
#define SLIDE_PSELECT_READY_TIMEOUT_USEC 250000
#define SLIDE_PSELECT_RECHECK_TIMEOUT_USEC 25000
#define SLIDE_PSELECT_WCHAN_CONFIRMATIONS 2
#define APP_PSELECT_TRIGGER_MAX_AGE_USEC 75000
#define APP_PSELECT_POST_GUARD_AGE_CHECK 1

/* Enforce local, fully described RB children before sched_setattr(). */
#define P0_ZZHL_RT_MUTEX_PREFLIGHT 1
/* Local RB gate only.  No FOPS/data-write release in this artifact. */
#define P0_ZZHL_LOCAL_RB_GATE_ONLY 1

#endif

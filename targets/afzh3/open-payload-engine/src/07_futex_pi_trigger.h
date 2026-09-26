#ifndef OSS_CLONE_FUTEX_PI_TRIGGER_H
#define OSS_CLONE_FUTEX_PI_TRIGGER_H

#include <stdint.h>

/* Real FUTEX_LOCK_PI + FUTEX_WAIT_REQUEUE_PI two-thread dance (waiter +
 * owner), then a third thread calls sched_setattr() on the waiter's tid
 * -- source: FUN_00103e18 (waiter, real futex setup) + FUN_00104300
 * (consumer, calls sched_setattr via FUN_001056a8 = syscall(0x112,...),
 * confirmed __NR_sched_setattr on arm64).
 *
 * This is the SAME technique this project's own src/slide_app.c already
 * implements (slide_waiter_thread/slide_owner_thread/
 * slide_consumer_thread) -- the futex chain setup itself has never once
 * been the crash site in any test this session (always reaches the
 * expected wait_requeue_pi ret=-1 errno=110 cleanly); only the object
 * content written before the trigger differed from the closed binary's
 * (see 04_fake_kernel_objects.c). Reusing the proven futex-thread shape here,
 * wiring it to the newly corrected object.
 *
 * Returns 1 if the trigger fired (task->pi_blocked_on requeue completed
 * without crashing), 0 on any setup failure before the trigger. */
int run_futex_trigger(void);

/* source: raw disasm of FUN_00103e18 this session (targets/afzh3/reference/kernel/
 * README.md, "Further trace of FUN_00103e18" section) -- the closed
 * binary calls its AAR/AAW verify step (FUN_001076c0) DIRECTLY from
 * inside the same code path that just fired the trigger, immediately
 * after, not from a separate caller much later. This project's earlier
 * test harnesses called oss_verify_kernel_access()/root_umh_install()
 * from main(), well after run_futex_trigger() had already fully
 * returned. post_trigger_cb, if non-NULL, is invoked exactly once,
 * from the same call stack, immediately after sched_setattr succeeds
 * (before run_futex_trigger returns) -- ctx is passed through
 * unmodified. Its return value becomes run_futex_trigger_cb's own
 * return value (still 1 = trigger+callback both reported success). If
 * post_trigger_cb is NULL, behaves exactly like the original
 * run_futex_trigger() (callback skipped, returns 1 once sched_setattr
 * succeeds) -- existing callers (test_futex_trigger.c, test_groom.c)
 * are unaffected. */
typedef int (*futex_post_trigger_cb)(void *ctx);
int run_futex_trigger_cb(futex_post_trigger_cb post_trigger_cb, void *ctx);

/* source: full FUN_00103e18 sequence including the sigaction/tgkill/
 * FPSIMD-payload mechanism (see src/06_signal_frame_payload.h) -- the most
 * faithful entry point in this project so far. page_base/
 * ashmem_misc_fops_addr are the SAME values groom_and_install_fops_object()
 * returns and 04_fake_kernel_objects.c's build_fops_install_object() already
 * takes -- used here to build the sigusr1 payload with real addresses
 * instead of the simpler entry points' silence on the subject.
 * post_trigger_cb behaves exactly as in run_futex_trigger_cb(). */
int run_futex_trigger_full(uint64_t page_base, uint64_t ashmem_misc_fops_addr,
                            futex_post_trigger_cb post_trigger_cb, void *ctx);

/* source: real kernel source (kernel/futex/core.c), not disassembly --
 * see 07_futex_pi_trigger.c's comment above these functions for the full
 * derivation. Diagnostic/experimental variant: same overall shape as
 * run_futex_trigger_full(), but the owner thread holds ONLY f_pi_target
 * (never contends for f_pi_chain), removing the artificial circular
 * wait that this project's inherited (from src/slide_app.c, proven
 * correct only for ITS unrelated purpose) dance was causing
 * FUTEX_CMP_REQUEUE_PI to deadlock-detect and bail via
 * Q_REQUEUE_PI_NONE/IGNORE -- confirmed via kprobes to never reach
 * rt_mutex_wait_proxy_lock()/rt_mutex_cleanup_proxy_lock() at all.
 * This variant expects CMP_REQUEUE_PI to succeed for real (ret>=0),
 * putting the waiter into a genuine PI-blocked wait it will time out
 * of naturally. */
int run_futex_trigger_success_cb(futex_post_trigger_cb post_trigger_cb,
                                  void *ctx);
int run_futex_trigger_success_full(uint64_t page_base,
                                    uint64_t ashmem_misc_fops_addr,
                                    futex_post_trigger_cb post_trigger_cb,
                                    void *ctx);

/* source: raw disasm of FUN_00104274 (owner), full derivation in
 * 07_futex_pi_trigger.c above run_futex_trigger_v3_cb(). Restores the
 * owner's second FUTEX_LOCK_PI(f_pi_chain) attempt (matching the
 * closed binary's real structure, dropped in the _success_ variant
 * above), but only after the requeue has already genuinely succeeded
 * -- testing whether the SECOND, later-timed collision between owner
 * and waiter is where rt_mutex_adjust_prio_chain()'s deadlock-cycle
 * walk touches the already-proxy-PI-blocked waiter's structure. Not
 * yet proven on-device. */
int run_futex_trigger_v3_cb(futex_post_trigger_cb post_trigger_cb, void *ctx);
int run_futex_trigger_v3_full(uint64_t page_base,
                               uint64_t ashmem_misc_fops_addr,
                               futex_post_trigger_cb post_trigger_cb,
                               void *ctx);

/* source: fresh, from-scratch re-disassembly this session of the REAL
 * .so (RootMyGalaxyDesktop/assets/cve-2026-43499-app-afzh3.so, matching
 * the connected device's exact firmware S918BXXSAFZH3), done to
 * double-check every earlier assumption byte-for-byte with
 * `asm.sub.var=false` (this r2 version renamed the old `asm.varsub`
 * eval var). Confirmed the waiter (fcn.00003e18) and owner
 * (fcn.00004274) thread bodies match run_futex_trigger_cb()'s ORIGINAL
 * (v1) waiter_thread_fn/owner_thread_fn exactly -- same lock order, same
 * ungated/unchecked second LOCK_PI, same handshake flags. No bug there.
 *
 * IMPORTANT CORRECTION made while double-checking this same session:
 * app_main (fcn.000044f4) actually spawns THREE threads, not two --
 * waiter (fcn.00003e18), owner (fcn.00004274), AND a separate consumer
 * (fcn.00004300, confirmed via `adr x2, fcn.00004300` at raw vaddr
 * 0x46d0, a third pthread_create call app_main makes). app_main itself
 * pins to CPU 0 (`mov x0,xzr` at 0x4580 right before its OWN
 * fcn.000041f0 call at 0x4584) and, in ITS OWN thread, later fires
 * FUTEX_CMP_REQUEUE_PI directly (raw vaddr 0x4744) -- CPU 1 belongs to
 * the SEPARATE consumer thread (fcn.00004300's own first action), which
 * is also the ONLY place sched_setattr (fcn.000056a8) is called from
 * anywhere in this binary. Real consumer's own path to that call is
 * gated behind an outer attempt-counter state machine (globals at
 * G+0x764/0x76c/0x768, G=.data+0xd000) whose writer this session did
 * NOT find (not fcn.00004300 itself, not app_main, not owner/waiter --
 * almost certainly this project's own already-implemented
 * S23_SUPERVISOR_ATTEMPT multi-process outer supervisor, out of scope
 * for the single-attempt corruption primitive). This means real
 * consumer's sched_setattr call may not fire at all in a single,
 * unsupervised run, or may fire on a LATER attempt while an EARLIER
 * attempt's kernel-side state is still settling -- a genuinely
 * different, not-yet-reproduced timing relationship this project has
 * never modeled. v4 below is an honest SIMPLIFICATION: it merges
 * app_main's CMP_REQUEUE_PI call and consumer's sched_setattr call into
 * one serial calling-thread flow (same structure as v1/v2/v3), pinned
 * to CPU 1 (borrowing consumer's real placement, since that call is
 * temporally last and closest to this project's own post-trigger verify
 * step) rather than CPU 0 (app_main's real self-pin, which matters less
 * here since our callback needs happen after the sched_setattr-equivalent
 * step, not around the CMP_REQUEUE_PI call itself). Four real,
 * previously-missed details were found and applied:
 *   1. The calling thread pins to CPU 1 (see honest caveat above) before
 *      its CMP_REQUEUE_PI polling loop. This project's
 *      run_futex_trigger_cb never pinned the calling thread at all.
 *   2. After observing (waiter_waiting OR owner_started -- CORRECTED
 *      during a final double-check: the G+0x744 flag app_main polls is
 *      set by the waiter at raw vaddr 0x3f28, right before its
 *      WAIT_REQUEUE_PI call, i.e. this project's own existing
 *      `waiter_waiting` flag, NOT waiter_tid_g/G+0x730 as first
 *      assumed -- waiter_tid_g is set far earlier, right after
 *      gettid(), well before LOCK_PI(f_pi_chain) even happens), app_main
 *      does a FIXED `usleep(100000)` (100ms, raw vaddr 0x470c: `mov
 *      w0,0x86a0; movk w0,1,lsl 16` = 0x186a0 = 100000) -- BEFORE
 *      calling FUTEX_CMP_REQUEUE_PI. Every earlier variant in this
 *      project polled tightly (1ms steps) and fired immediately once
 *      both flags were set, with no equivalent pause. Given the owner's
 *      second LOCK_PI(f_pi_chain) fires immediately after owner_started
 *      with nothing in between, this 100ms gap almost certainly changes
 *      whether the owner's second lock or the CMP_REQUEUE_PI call wins
 *      the race in the kernel -- not yet proven which way, needs
 *      on-device kprobe testing.
 *   3. sched_setattr's attr.sched_policy is NOT 0 (SCHED_OTHER, what
 *      every earlier variant implicitly sent via memset-to-0) -- the
 *      real .rodata constant at raw vaddr 0x22c0 (loaded as one 8-byte
 *      `ldr d1,[x10,0x2c0]` covering attr.size+attr.sched_policy
 *      together) is bytes `30 00 00 00 03 00 00 00` = size=0x30(48,
 *      matches sizeof(struct local_sched_attr) exactly), policy=3
 *      (SCHED_BATCH). A POLICY change (not just a nice value change)
 *      takes a different, more invasive kernel path than a same-policy
 *      renice -- plausibly load-bearing for whether rt_mutex_adjust_pi()
 *      fires as expected.
 *   4. nice_value is not the constant 1 every earlier variant hardcoded
 *      -- raw disasm of fcn.00004300 (a SEPARATE, higher-level
 *      attempt-supervisor function, not part of the single-attempt
 *      dance -- see targets/afzh3/reference/kernel/README.md for why it and the
 *      G+0x734/G+0x714/fork()+kill()+waitpid() logic right after
 *      app_main's own CMP_REQUEUE_PI call are judged out-of-scope
 *      outer-process supervision, already covered by this project's own
 *      S23_SUPERVISOR_ATTEMPT harness) shows `nice = ((attempt_1based -
 *      1) % 8) + 0x13(19)`. For a single default attempt (attempt=1,
 *      the common case this project's test harnesses actually exercise)
 *      that's nice=19, not 1.
 *
 * This variant is the MOST faithful single-attempt reproduction of
 * app_main's real sequence built in this project so far -- same
 * waiter/owner bodies already proven byte-accurate, plus these four
 * corrections. Not yet proven on-device (built while the device was
 * disconnected overnight); expected to still hit EDEADLK at
 * CMP_REQUEUE_PI like the original dance does (the 100ms delay makes
 * the owner's second lock win the race MORE likely, not less, if
 * anything) -- if so, that is itself a meaningful result: it would mean
 * EDEADLK-then-outer-retry is the REAL, intended normal-case behavior of
 * the closed binary, not a bug in this project's reproduction. */
int run_futex_trigger_v4_cb(futex_post_trigger_cb post_trigger_cb, void *ctx);
int run_futex_trigger_v4_full(uint64_t page_base,
                               uint64_t ashmem_misc_fops_addr,
                               futex_post_trigger_cb post_trigger_cb,
                               void *ctx);

/* source: same fresh re-disassembly as v4 (see its comment above and
 * 07_futex_pi_trigger.c/targets/afzh3/reference/kernel/README.md for full derivation),
 * fixing v4's one acknowledged simplification: the real binary spawns a
 * genuinely SEPARATE, fourth thread (fcn.00004300, "consumer", pinned
 * to CPU 1) that fires sched_setattr on the waiter's tid on ITS OWN
 * timeline -- NOT sequenced after CMP_REQUEUE_PI's result, NOT waiting
 * for route_done, coupled to the rest of the dance only loosely (via
 * the waiter's cached tid, which is set very early, right after
 * gettid(), before the waiter's first LOCK_PI call even happens). This
 * project could not find what really gates the real consumer's exact
 * firing instant (judged to be outer, multi-process supervisor state --
 * see the README), so this variant fires the consumer's sched_setattr
 * call as soon as the waiter's tid is known -- the earliest defensible
 * substitute, maximizing genuine overlap with the owner's second lock
 * attempt, the waiter's WAIT_REQUEUE_PI call, and app_main's
 * CMP_REQUEUE_PI call, all of which may now be racing concurrently
 * across four real CPUs (app_main@0, waiter@3, owner unpinned,
 * consumer@1) for the first time in this project's testing history.
 * Not yet proven on-device (built while the device was disconnected). */
int run_futex_trigger_v5_cb(futex_post_trigger_cb post_trigger_cb, void *ctx);
int run_futex_trigger_v5_full(uint64_t page_base,
                               uint64_t ashmem_misc_fops_addr,
                               futex_post_trigger_cb post_trigger_cb,
                               void *ctx);

/* source: fresh re-disassembly this session, raw vaddr 0x40d8-0x4160
 * (waiter, fcn.00003e18) cross-referenced against raw vaddr 0x44ac-
 * 0x44bc (consumer, fcn.00004300) -- a real inter-thread handshake this
 * project had never found or ported before, missed even by the
 * "fresh re-disassembly" pass that produced v4/v5 (the waiter's call to
 * the verify function, fcn.000076c0, sits at raw vaddr 0x4150, a single
 * instruction this project's earlier read of the waiter jumped past).
 * After the SIGUSR1 handler confirms success (this project's existing
 * check, unchanged), the real waiter does NOT immediately proceed to
 * UNLOCK_PI -- it first (a) writes 1 to a global at G+0x76c (G =
 * .data+0xd000; this is the actual writer of the flag this project's
 * STATUS.md previously guessed, incorrectly, belonged to an outer
 * multi-process supervisor -- it does not, it is set by this project's
 * own waiter thread every run), (b) busy-waits (tight `yield` loop, up
 * to ~1e9 iterations) for a SEPARATE global at G+0x750 to become
 * nonzero, (c) resets G+0x76c back to 0, then (d) calls the verify
 * function fcn.000076c0 -- but ONLY if a THIRD global at G+0x760 is
 * >= 1. The real consumer thread (fcn.00004300) is what sets G+0x750=1,
 * unconditionally, immediately after its own sched_setattr call
 * returns (success or failure) -- and only atomically increments
 * G+0x760 when that sched_setattr call specifically returned 0
 * (success). Net effect: the real binary calls verify() from INSIDE
 * the waiter thread, gated on the SEPARATE consumer thread's
 * sched_setattr having already genuinely succeeded -- not, as every
 * earlier variant in this project did, from the calling/main thread,
 * unconditionally, after just waiting for route_done. Ported as new
 * globals mirroring G+0x750/G+0x760 (`g_sched_setattr_done`/
 * `g_sched_setattr_ok`) and a waiter variant that calls
 * post_trigger_cb directly, matching this real ordering exactly. Not
 * yet proven on-device. */
int run_futex_trigger_v6_cb(futex_post_trigger_cb post_trigger_cb, void *ctx);
int run_futex_trigger_v6_full(uint64_t page_base,
                               uint64_t ashmem_misc_fops_addr,
                               futex_post_trigger_cb post_trigger_cb,
                               void *ctx);

/* source: raw vaddr 0x4360-0x43ac (consumer, fcn.00004300), fresh
 * re-disassembly this session, cross-referenced against real kernel
 * source (targets/afzh3/reference/kernel/locking/rtmutex_api.c:436,
 * `rt_mutex_adjust_pi()`) -- fixes a real, significant timing bug in
 * `v4`/`v5`/`v6`: the consumer thread does NOT fire `sched_setattr`
 * as soon as the waiter's tid is known. Its real gating condition
 * (`w20 = *(G+0x76c); if (w20 == 0) { yield; goto top; }`, `G` =
 * .data+0xd000) means it TIGHT-SPINS, never calling `sched_setattr`
 * at all, until G+0x76c becomes nonzero -- and G+0x76c is written
 * ONLY by the WAITER thread itself (raw vaddr 0x40fc), and ONLY after
 * its SIGUSR1 handler has already confirmed success (see
 * run_futex_trigger_v6_cb()'s comment above for that handshake's other
 * half). This project's v4/v5/v6 all had the consumer fire
 * `sched_setattr` essentially at t=0 (as soon as `waiter_tid_g` was
 * set, right after the waiter's own `gettid()`, long before it even
 * attempts its first `LOCK_PI`) -- completely missing the real timing,
 * where `sched_setattr`'s kernel-side `rt_mutex_adjust_pi()` (confirmed
 * via kprobe this session to genuinely fire, but with
 * `task->pi_blocked_on == NULL` every time under the wrong, t=0 timing
 * -- consistent with firing before the waiter is ever blocked on
 * anything) runs immediately AFTER, not long before, the waiter's own
 * SIGUSR1/FPSIMD payload delivery. This is the first variant in this
 * project to fire `sched_setattr` at the temporally-correct moment.
 * Not yet proven on-device. */
int run_futex_trigger_v7_cb(futex_post_trigger_cb post_trigger_cb, void *ctx);
int run_futex_trigger_v7_full(uint64_t page_base,
                               uint64_t ashmem_misc_fops_addr,
                               futex_post_trigger_cb post_trigger_cb,
                               void *ctx);

/* source: NOT from the closed binary -- this project's own hypothesis,
 * derived from real kernel source this session (targets/afzh3/reference/kernel/
 * locking/rtmutex.c, rt_mutex_adjust_prio()/rt_mutex_enqueue_pi()/
 * task_top_pi_waiter()). See 07_futex_pi_trigger.h's comment above v7 and
 * STATUS.md's "Deep kernel-source trace" section for the full
 * derivation. Summary: task_top_pi_waiter(p)->task (used by
 * rt_mutex_adjust_prio() -> rt_mutex_setprio()) reads a task's
 * pi_waiters rb-tree DIRECTLY, with no re-validation against the
 * waiter's own .lock field -- unlike the crash-prone path this project
 * already ruled out (task->pi_blocked_on with a NULL .lock, confirmed
 * fatal). For this fake-waiter payload's `task` field to ever be read
 * this way, a REAL waiter must already be validly linked into some
 * task's pi_waiters (this project's own dance already does this: the
 * owner thread's second LOCK_PI(f_pi_chain) call, once blocked, links
 * owner's own stack-local rt_mutex_waiter into the WAITER task's
 * pi_waiters), and then have its backing memory freed/reused WHILE
 * still linked -- a genuine UAF, not a fabrication. This variant tests
 * whether interrupting the OWNER thread (via a non-fatal signal) while
 * it is blocked inside that second LOCK_PI call opens such a window,
 * by forcing the kernel's interrupted-PI-wait cleanup path to run.
 * Not yet attempted before this variant -- deliberately not run blind;
 * built with a dedicated kprobe on rt_mutex_setprio() (its 2nd arg,
 * pi_task, is exactly the value this whole chain is trying to
 * corrupt) planned for the same test pass. */
int run_futex_trigger_v8_cb(futex_post_trigger_cb post_trigger_cb, void *ctx);
int run_futex_trigger_v8_full(uint64_t page_base,
                               uint64_t ashmem_misc_fops_addr,
                               futex_post_trigger_cb post_trigger_cb,
                               void *ctx);

/* source: real kernel source obtained THIS session
 * (targets/afzh3/reference/kernel/futex/core.c, extracted from
 * ~/Downloads/SM-S918B_16_Opensource/Kernel.tar.gz -- the Samsung GPL
 * release for this exact device/build, not available locally before
 * now) -- fixes v8's own bug. `futex_lock_pi()` (core.c:3040) blocks
 * via `rt_mutex_wait_proxy_lock()` (targets/afzh3/reference/kernel/locking/
 * rtmutex_api.c:354), which explicitly uses `TASK_INTERRUPTIBLE`
 * (line 362-363) -- confirming a signal SHOULD interrupt owner's
 * blocked second LOCK_PI, contrary to v8's apparent negative result.
 * Re-examining v8: it sent SIGUSR2 then almost immediately let the
 * waiter proceed to its own real UNLOCK_PI call -- the legitimate
 * wakeup very likely raced the signal and won. This variant adds a
 * deliberate delay between signaling owner and the waiter's UNLOCK_PI,
 * giving the signal time to actually land first. If it lands,
 * `futex_lock_pi()` runs its `cleanup:` path (core.c:3152-3164),
 * calling `rt_mutex_cleanup_proxy_lock()` -- exactly the function this
 * project has a kprobe for already, never observed firing before now.
 * Not yet proven on-device. */
int run_futex_trigger_v9_cb(futex_post_trigger_cb post_trigger_cb, void *ctx);
int run_futex_trigger_v9_full(uint64_t page_base,
                               uint64_t ashmem_misc_fops_addr,
                               futex_post_trigger_cb post_trigger_cb,
                               void *ctx);

/* source: fresh raw r2 disasm of the REAL closed binary this session
 * (RootMyGalaxyDesktop/assets/cve-2026-43499-app-afzh3.so,
 * fcn.00003e18 raw vaddr 0x40d4-0x4164, fcn.00004300 raw vaddr
 * 0x4494-0x44bc), not a hypothesis -- corrects two real divergences
 * from v8/v9 (both of which were this project's own invented
 * experiments, not confirmed against the binary):
 *
 * 1. v8/v9 send SIGUSR2 to the OWNER thread and sleep 300ms before
 *    releasing the consumer. The real waiter (0x40d4-0x40e4) does
 *    NEITHER: right after tgkill(SELF, SIGUSR1) returns and its own
 *    handler's completion flag reads 1, it immediately releases the
 *    consumer (equivalent of G+0x76c=1) with zero added delay. No
 *    SIGUSR2/owner-interrupt step exists in this exact code region.
 * 2. v8/v9's waiter waits for the consumer's sched_setattr completion
 *    via a `usleep(1000)`-based poll loop -- this DESCHEDULES the
 *    waiter thread between checks. The real waiter (0x4100-0x4128)
 *    busy-spins on a bare ARM64 `yield` instruction instead (bounded
 *    ~1s via a huge iteration cap, 0x3b9ac9ff), staying continuously
 *    RUNNABLE the entire time `sched_setattr(waiter_tid, ...)` is
 *    being applied to it from the consumer thread on a different
 *    pinned CPU. This project's `usleep`-based wait means the waiter
 *    is very likely actually SLEEPING (TASK_INTERRUPTIBLE, off-CPU)
 *    at the exact moment sched_setattr fires on it in every v8/v9 run
 *    so far -- a fundamentally different scheduler state than the real
 *    binary's continuously-runnable spin, and the most concrete,
 *    disassembly-confirmed candidate yet for why the corruption has
 *    never landed in 61+ oracle-monitored runs. Ported as a tight
 *    `yield`-loop bounded by CLOCK_MONOTONIC (~1s), matching the real
 *    binary's behavior without hardcoding a raw cycle count that would
 *    not port meaningfully across CPUs. */
int run_futex_trigger_v10_cb(futex_post_trigger_cb post_trigger_cb,
                              void *ctx);
int run_futex_trigger_v10_full(uint64_t page_base,
                                uint64_t ashmem_misc_fops_addr,
                                futex_post_trigger_cb post_trigger_cb,
                                void *ctx);

/* source: v10's own comment claimed "G+0x760 never written to a nonzero
 * value anywhere r2 can resolve statically" and concluded the real
 * waiter never calls fcn.000076c0 from this call site -- WRONG, found
 * by re-disassembling the consumer (fcn.00004300, raw vaddr 0x4494-
 * 0x44ac) with `pd` instead of relying on `axt`/xref search: the real
 * consumer's write to G+0x760 is `bl 0x3650`, and 0x3650 is
 * `__aarch64_atomic_fetch_add4_relax` (real instruction at raw vaddr
 * 0x3660: `ldaddal w0, w0, [x1]` -- an LSE atomic read-modify-write,
 * confirmed by disassembling 0x3650 directly). A static "find all str
 * writing a nonzero constant" xref search, or a naive `axt` call-graph
 * walk that doesn't resolve through this shared atomic-increment
 * helper, will never surface this as a "write" -- which is exactly
 * why v10's from-scratch search came back empty. The gate DOES open:
 * G+0x760 is incremented (matching this project's own
 * g_sched_setattr_ok, already set correctly by consumer_thread_fn_v8
 * on sched_setattr success) every time sched_setattr succeeds, BEFORE
 * G+0x750 (g_sched_setattr_done) is set, and the waiter's `b.lt 0x4164`
 * / `bl 0x76c0` (raw vaddr 0x4148-0x4154) genuinely executes the verify
 * call from inside the waiter thread when that gate is open -- exactly
 * the run_futex_trigger_v6_cb() call pattern this project had already
 * built and then abandoned in v10 on the strength of the disproven
 * "never opens" claim. v11 = v10's waiter body (no SIGUSR2/delay to
 * owner, yield-spin instead of usleep -- both still correct, unrelated
 * to this fix) with v6's callback-from-waiter-thread gate restored in
 * place of v10's callback-from-main-thread-after-join substitute. */
int run_futex_trigger_v11_cb(futex_post_trigger_cb post_trigger_cb,
                              void *ctx);
int run_futex_trigger_v11_full(uint64_t page_base,
                                uint64_t ashmem_misc_fops_addr,
                                futex_post_trigger_cb post_trigger_cb,
                                void *ctx);

/* source: re-disassembled raw vaddr 0x40f4-0x4128 one more time,
 * instruction by instruction, specifically to check what `0x3b9ac9ff`
 * actually bounds. It is a PLAIN LOOP-ITERATION COUNTER (`mov
 * x9,xzr` then `cmp x9,x10; add x9,x9,1; b.lo loop`), not a cycle-time
 * comparison -- the `mrs x11,cntvct_el0` at 0x40f8 reads the counter
 * but that value is dead (overwritten at 0x4118 by `ldar w11,[x8]`
 * before ever being read), a compiler leftover, not used for timing.
 * v10/v11 ported this as a CLOCK_MONOTONIC-bounded ~1s wall-clock
 * spin instead -- roughly similar real-world duration, but a
 * genuinely different loop body (calls clock_gettime() every
 * iteration, ~10-50x more overhead per iteration than the real
 * loop's 4-instruction body, and depends on this device's core
 * frequency for how many real iterations that corresponds to,
 * instead of a fixed count). v12 = v11 with this loop replaced by the
 * exact byte-accurate iteration count. This does not identify a root
 * cause for the sched_setattr-time crash (extensive tracing of the
 * real kernel source, both rt_mutex_dequeue_pi()'s rb_erase path for
 * this project's exact fake-waiter field values and
 * __sched_setscheduler()/rt_mutex_setprio()'s own call chain, found
 * nothing that should crash for this exact scenario in isolation --
 * see STATUS.md's 2026-09-18 session for the full trace) -- it closes
 * the one remaining known byte-inaccuracy in this exact code region,
 * on the chance that the loop's real duration/CPU-time-shape matters
 * for a race this project cannot see from source alone. */
int run_futex_trigger_v12_cb(futex_post_trigger_cb post_trigger_cb,
                              void *ctx);
int run_futex_trigger_v12_full(uint64_t page_base,
                                uint64_t ashmem_misc_fops_addr,
                                futex_post_trigger_cb post_trigger_cb,
                                void *ctx);

/* source: NOT from the closed binary -- deliberate bisection of Part 3's
 * two simultaneous changes (v8/v9, dozens of safe runs -> v10/v11/v12,
 * 5/5 crashes at the identical point), requested explicitly after
 * exhaustive static kernel-source tracing (see STATUS.md's 2026-09-18
 * "root cause NOT found" section) could not identify a root cause and
 * this device has no crash forensics (empty pstore/ramdump, adb drops
 * on panic -- a crashing run yields zero diagnostic data beyond
 * "crashed"). Two axes changed at once between the safe and crashing
 * variants: (A) whether the waiter sends SIGUSR2 + delay to the owner
 * thread before releasing the consumer, and (B) whether the waiter
 * waits for the consumer's sched_setattr completion via usleep-polling
 * (descheduled, TASK_INTERRUPTIBLE) or a tight yield busy-spin (stays
 * RUNNABLE). v13 isolates axis B alone: SIGUSR2+delay KEPT (axis A
 * unchanged from the safe v9 baseline), busy-spin swapped in (axis B
 * matches the crashing v10/v11/v12, using v12's byte-accurate
 * iteration-count bound). If v13 crashes, busy-spin alone is
 * sufficient to reproduce it, independent of the SIGUSR2 change. */
int run_futex_trigger_v13_cb(futex_post_trigger_cb post_trigger_cb,
                              void *ctx);
int run_futex_trigger_v13_full(uint64_t page_base,
                                uint64_t ashmem_misc_fops_addr,
                                futex_post_trigger_cb post_trigger_cb,
                                void *ctx);

/* source: closed FUN_00103e18/FUN_00104300/FUN_001044f4. v14 preserves
 * the two-phase signal/scheduler handshake: waiter publishes -1,
 * consumer acknowledges it, waiter rewrites FPSIMD and publishes 1,
 * then both threads stay runnable with atomic/cntvct/yield loops until
 * the consumer issues sched_setattr. No libc call or descheduling lies
 * between rt_sigreturn and sched_setattr. The main thread waits for
 * both futex readiness flags before the fixed 100ms requeue delay. */
int run_futex_trigger_v14_cb(futex_post_trigger_cb post_trigger_cb,
                              void *ctx);
int run_futex_trigger_v14_full(uint64_t page_base,
                                uint64_t ashmem_misc_fops_addr,
                                futex_post_trigger_cb post_trigger_cb,
                                void *ctx);
/* Debug instrumentation (H0): prints a timed marker relative to the v14
 * handshake entry. Safe to call from the waiter callback. */
void futex_v14_dbg(const char *name);

int run_futex_trigger_v14_full_staged(
    uint64_t page_base, uint64_t ashmem_misc_fops_addr,
    int32_t *mutation_state, int32_t pending_state, int32_t mutated_state,
    futex_post_trigger_cb post_trigger_cb, void *ctx);

#endif

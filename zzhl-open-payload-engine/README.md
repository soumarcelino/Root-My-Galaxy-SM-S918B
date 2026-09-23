# ZZHL Open Payload Engine

Port de endereços do código aberto para o firmware `S918BXXUAZZHL` do
`SM-S918B`. Esta pasta foi duplicada de `afzh3-open-payload-engine`; código, scripts e
evidências AFZH3 anteriores continuam preservados no clone original. Os
endereços do novo alvo estão em `src/target_zzhl.h`, derivados de
`/home/matias/Projects/build-do-firmware/dump/kernel.raw` e
`kernel.kallsyms`. `tools/profiles/zzhl.json` é o perfil de auditoria padrão.

`make -B all so` compila a rota legada ZZHL. `make p0` compila a rota P0
isolada em `build/oss_clone_p0_zzhl.so`; ela preserva a rota legada e exige
`tools/audit-p0-zzhl.py` antes do link. O payload ZZHL ainda não tem prova de
root, KernelSU ou estabilidade em boots limpos. O helper externo, runner e
evidências históricas abaixo pertencem ao fluxo AFZH3; não os trate como
validação do novo payload.

## Histórico da base AFZH3

Clean-room C reconstruction of `cve-2026-43499-app-afzh3.so`, the closed
GhostLock payload for Samsung Galaxy S23 Ultra
`dm3q-S918BXXSAFZH3`. Ghidra decompile, raw disassembly, matching Samsung
kernel source, live BTF/kallsyms, and laboratory-device checks provide the
reference behavior.

## Current status

Complete source chain is connected:

1. `_INIT_2`-style supervisor forks bounded attempts; each attempt raises
   `RLIMIT_NOFILE` and `RLIMIT_NPROC` before kernel work.
2. Tracefs resolves KASLR; grooming leaks an `mm_struct` candidate and derives
   aligned base `A = candidate & ~0x7fff`.
3. Reclaim sends the closed layout as an exact `0x8e80` skb buffer. Its data
   starts at `D = A - 0xe80`; therefore buffer `+0x2000` lands at live address
   `A+0x1180`. The builder emits one fake FOPS table at that buffer offset.
   The order-3 drain is split exactly 16 prepare slabs before and 16 after the
   target/leak phase. Reclaim tries at most 64 nonblocking sends.
4. Futex trigger v14 uses the closed globals' page offsets, installs SIGUSR1
   from the waiter, and performs the `-1 → gate → SIGUSR1 → 1 → sched_setattr`
   handshake. The consumer delay is zeroed after `WAIT_REQUEUE_PI`; the main
   thread sleeps 10 ms while waiting instead of occupying CPU0.
5. Immediate callback opens the shell-accessible ashmem alias. Discovery tries
   `/dev/ashmem<boot_id>`, then matching `/dev/ashmem*` character devices with
   the canonical `st_rdev`; an inaccessible path aborts before grooming.
6. AAR/AAW verifies the corrupted `ashmem_misc.fops` and restores the global
   pointer before the root stage.
7. `pipe_physrw` reproduces the closed 2-bank pipe geometry and provides the
   dynamic physical read/write backend. Configfs remains limited to SELinux
   state and the static workqueue slot, matching the closed payload.
8. `root_umh` publishes the work item, validates completion/root socket, and
   late-loads KernelSU.

Android app and shared-object variants compile. Host geometry test passes. The
current artifact `bf41c36892a8fa69eddb761e3bbf7ab2a61b887c213e1aa53686bf92cde33a3f`
passed a 5/5 clean-boot soak with the screen left on and a load/thermal/PSI gate.
Every run reached `temporary-root-ready`, verified KernelSU control, and
returned `uid=0(root)` from `/system/bin/su -c id`.

## Method

- Ported logic cites closed functions with `source: FUN_xxxxxxxx` comments.
- Constants come from closed `.rodata`/`.data`, raw AArch64 instructions,
  matching kernel layouts, or live target evidence.
- Ambiguous behavior remains explicit; current success claims stop at observed
  tests.
- `STATUS.md` keeps the chronological investigation and superseded states.

## Documentation

The complete technical package is indexed in [`docs/README.md`](docs/README.md).
It covers methodology, architecture, root causes, pipe physical R/W, workqueue
UMH, binary-fidelity mapping, two-reboot validation, operational runbook,
forensics, maintenance limits, and the tools/evidence used during the port.

## Tools

Reusable porting and debug scripts are documented in
[`tools/README.md`](tools/README.md). The suite includes read-only device
preflight, forensic collection, BTF layout extraction, kallsyms offset
derivation with masked-address rejection, build manifests, ELF comparison,
run-log classification, target profiles, repository checks, and a gated
two-clean-reboot validator.

## Main components

| Component | Role |
|---|---|
| `src/main.c` | Constructor/executable entry, limits, attempt supervisor, complete chain wiring. |
| `src/kaslr.c`, `src/slabinfo.c` | Kernel-base discovery and slab telemetry. |
| `src/groom.c` | Exact object leak, split drain, and skb reclaim lifecycle. |
| `src/fops_install.c` | Exact fake waiter/FOPS/task buffer layout using aligned base A. |
| `src/sigusr1_payload.c`, `src/futex_trigger.c` | FPSIMD transport and v14 futex/scheduler trigger. |
| `src/aar_aaw.c` | Ashmem alias discovery, kernel read/write wrappers, verification. |
| `src/pipe_physrw.c` | Closed-layout pipe-buffer physical read/write backend. |
| `src/root_umh.c` | Workqueue usermode-helper root stage. |

Focused diagnostics live under `tests/`; their support-only implementations
live under `tests/support/`. They never enter the production payload.

## Build

Set `ANDROID_NDK_HOME` to an NDK containing the requested API compiler, then:

```sh
make          # build/app_main
make so       # build/oss_clone_payload.so
make p0       # build/oss_clone_p0_zzhl.so; P0 auditado e separado
make tests    # build focused diagnostics under build/tests/
```

End-to-end execution changes kernel state. Use only an authorized laboratory
device. Final acceptance requires two independent clean reboots with boot ID,
uptime, full payload log, and `su -c id` captured for each run.

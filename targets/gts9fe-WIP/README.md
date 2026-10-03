# SM-X518U X518UVLSFEZG3 target · Galaxy Tab S9 FE 5G

Target identity: `X518UVLSFEZG3` on Android 16 (One UI 8.5, `ro.build.version.oneui`
`80500`), with kernel `5.15.189-android13-3-33478785`
(`#1 SMP PREEMPT Sat Jul 18 02:16:22 KST 2026`). SoC is the Exynos 1380
(`s5e8835`, `ro.board.platform` `erd8835`): 4×Cortex-A55 at 2.0 GHz and
4×Cortex-A78 at 2.4 GHz, 5.3 GiB RAM, 4 KiB pages. This is not the Snapdragon
8 Gen 2 used by every other target in this repository.

The boot image was recovered from Samsung's FUS firmware
(`X518UVLSFEZG3.zip`) through `samloader-rs`. The derived kallsyms (127,923
symbols) and BTF are kept under
[`reference/kernel/`](reference/kernel/) so the contract can be re-derived
without the 9.7 GB firmware archive.

This target uses the **runtime-contract engine** (`universal-WIP` lineage): the
100 numeric fields live in `app/src/main/assets/bope-contract-gts9fe.txt`
rather than in a firmware-specific `target.h`. The engine's
`src/runtime_contract.c` parses `BOPE_TARGET_CONTRACT` and checks the exact
model, device, incremental build, fingerprint, kernel release/version, and boot
ID before the first attempt. `brazilian-open-payload-engine/src/target.h`
retains only the inherited object geometry and calibrated timing.

KernelSU Next v3.4.0 is built against the `android13-5.15` KMI with the Samsung
KDP/RKP/DEFEX port and an exact `5.15.189-android13-3-33478785` vermagic; see
[`kernelsu-next/README.md`](kernelsu-next/README.md).

## Validation

Device-validated on hardware: root reached in 6 seconds and KernelSU Next
v3.4.0 loaded. Full record, artifact hashes, and the two-run policy are in
[`brazilian-open-payload-engine/docs/07-VALIDACAO-DEVICE.md`](brazilian-open-payload-engine/docs/07-VALIDACAO-DEVICE.md).

## Target-specific notes

- Thermal zones are not readable by the shell/app UID on this device, so the
  stability launcher treats a missing sensor as unavailable instead of fatal.
- The pipe probe uses 240 pipes: pipe capacity is a per-UID budget
  (`fs.pipe-user-pages-soft`) and the engine's own banks already hold ~480.
- `worker_thread+0xbc` is the `sched_blocked_reason` caller here, not the
  inherited `+0x78`; `tools/bope/contract.py` derives it by disassembly.
- KASLR granularity is 0x8000, measured on hardware across two boots.

Building and installing the APK does not execute the payload.

# Simple Root :: ZZI8, the latest One UI 9 Beta 2 firmware

`simple-root.sh` is the hands-on command-line route for the exact
`SM-S918B / S918BXXUAZZI8`, the latest One UI 9 Beta 2 firmware. It builds
BOPE and the ZZI8 Stability Launcher, stages their assets through ADB, obtains
temporary root, late-loads KernelSU Next v3.4.0, restores SELinux enforcing,
and verifies `su -c id`.

The target is hardcoded to `targets/zzi8-WIP/`. There is no environment or
command-line target selector. Before compiling or staging anything, the runner
requires an exact match for model, device, build display, kernel release, and
kernel version. Regional fingerprint prefixes are diagnostic only. Every other
firmware is rejected.

## Exact target

```text
model: SM-S918B
device: dm3q
build: S918BXXUAZZI8 (latest One UI 9 Beta 2 firmware)
build display: CP2A.260605.016.S918BXXUAZZI8
fingerprint: samsung/dm3qxxx/dm3q:17/CP2A.260605.016/S918BXXUAZZI8:user/release-keys
kernel release: 5.15.197-android13-8-34343818-abS918BXXUAZZI8
kernel version: #1 SMP PREEMPT Mon Sep 14 06:56:00 UTC 2026
```

## Build and assets

At each run, the script:

1. Confirms that the connected device matches the complete latest One UI 9
   Beta 2 firmware (`ZZI8`) identity.
2. Verifies the latest One UI 9 Beta 2 firmware's ZZI8 KernelSU Next v3.4.0
   SHA-256 manifest, Samsung KDP marker, and exact kernel vermagic.
3. Verifies `src/target.h` independently against the latest One UI 9 Beta 2
   firmware's ZZI8 ELF and BTF through `tools/bope-verify-target`.
4. Builds the latest One UI 9 Beta 2 firmware's ZZI8 engine `so` target, which
   produces BOPE, the mm factory, and `stability-launcher-zzi8` with its
   target-specific gate.
5. Builds the open ZZI8 helper for the latest One UI 9 Beta 2 firmware, used by
   the usermode-helper and KernelSU late-load paths.

The Android NDK must be installed. `ANDROID_NDK_HOME` or `ANDROID_NDK_ROOT` may
select it; the script falls back to NDK `28.2.13676358` under
`$HOME/Android/Sdk/ndk/`.

The resulting runtime bundle is:

| Asset | Latest One UI 9 Beta 2 firmware (`ZZI8`) source |
| --- | --- |
| `assets/payload.so` | `targets/zzi8-WIP/brazilian-open-payload-engine/build/payload.so` |
| `assets/mm-exec-factory` | `targets/zzi8-WIP/brazilian-open-payload-engine/build/mm-exec-factory` |
| `assets/stability-launcher` | `targets/zzi8-WIP/brazilian-open-payload-engine/build/stability-launcher-zzi8` |
| `assets/ksu-helper` | `targets/zzi8-WIP/helper/build/cve-2026-43499-root` |
| `assets/ksud-selected` | `targets/zzi8-WIP/kernelsu-next/out/kernelsu-next-zzi8-v3.4.0/ksud-next-v3.4.0` |
| `assets/android13-5.15_kernelsu.ko` | `targets/zzi8-WIP/kernelsu-next/out/kernelsu-next-zzi8-v3.4.0/android13-5.15_kernelsu.ko` |

`ksud-selected` contains the audited ZZI8 module for the latest One UI 9 Beta 2
firmware, used for late-load. The separate `.ko` copy stays in the bundle as
inspectable compatibility evidence.

## Run

Start from a clean boot of ZZI8, the latest One UI 9 Beta 2 firmware. With
exactly one authorized ADB device:

```sh
cd simple-root
./simple-root.sh
```

Pass the serial explicitly when more than one device is connected:

```sh
./simple-root.sh RXCX602E20X
```

The runner copies the six assets to `/data/local/tmp/simple-root/`, validates
ELF magic while staging runtime files, and starts the Stability Launcher. The
launcher waits for a stable allocator/system window and permits up to three
bounded BOPE attempts. BOPE retries only before kernel mutation; a post-mutation
failure requires a clean reboot.

After temporary root, the helper late-loads KernelSU Next, checks its control
channel, restores SELinux enforcing, and waits up to 30 seconds for
`uid=0(root)`. On success, the script opens `adb shell su`.

If root is already active, the script still rebuilds and stages the exact ZZI8
bundle for the latest One UI 9 Beta 2 firmware, reports the existing root
identity, and exits without another payload attempt.

`SLIDE_P0_OFFSET` may be set only to a known aligned offset for the current boot.
The runner also accepts integer diagnostic overrides for `FUTEX_WAIT_SEC`,
`KSNITCH_REPEAT`, `KSNITCH_APPENDED`, `PIPE_DETERMINISTIC`, and
`RMG_TRACE_FILE`. These do not change the firmware target.

# Simple Root

`simple-root.sh` builds the AFZH3 payload and stability launcher, stages their
assets on an Android device, runs the launcher gate, loads the payload through
`LD_PRELOAD`, loads KernelSU, and checks `su -c id`.

## Build and assets

At each run, the script:

1. Verifies the AFZH3 KernelSU Next v3.4.0 hashes and module compatibility,
   then copies `ksud-next-v3.4.0` to `assets/ksud-selected` and
   `android13-5.15_kernelsu.ko` to `assets/android13-5.15_kernelsu.ko`.
   The helper starts `ksud-selected`, which contains the module for late-load.
2. Compiles `../stability-launcher/stability-launcher.c` for Android ARM64/API
   35 and copies the binary to `assets/stability-launcher`.
3. Builds only the shared-library target in `../afzh3-open-payload-engine/`
   and copies `build/payload.so` to `assets/payload.so`.
4. Builds the AFZH3 helper from `src/su_daemon.c` for Android ARM64/API 35 and
   copies it to `assets/ksu-helper`.

The payload itself is produced by the open payload engine. `app_main` is not
built by this runner; the runner requests only the engine's `so` target. The
Android NDK must be installed. `ANDROID_NDK_HOME` or `ANDROID_NDK_ROOT` may
select an NDK; the script falls back to NDK `28.2.13676358` under
`$HOME/Android/Sdk/ndk/`.

The resulting asset set is:

| Asset | Source |
| --- | --- |
| `assets/payload.so` | `afzh3-open-payload-engine/build/payload.so` |
| `assets/stability-launcher` | `stability-launcher/build/stability-launcher` |
| `assets/ksu-helper` | `src/su_daemon.c` via `simple-root/build/cve-2026-43499-root` |
| `assets/ksud-selected` | `kernelsu-next/out/kernelsu-next-afzh3-v3.4.0/ksud-next-v3.4.0` |
| `assets/android13-5.15_kernelsu.ko` | `kernelsu-next/out/kernelsu-next-afzh3-v3.4.0/android13-5.15_kernelsu.ko` |

## Run

Pass the ADB serial when multiple devices are connected:

```sh
./simple-root.sh RXCX602E20X
```

With exactly one authorized ADB device, the serial may be omitted:

```sh
./simple-root.sh
```

After building and validating assets, the script copies the five listed files
to `/data/local/tmp/simple-root/`. It also stages the runtime files
at their individual paths in `/data/local/tmp`, validates ELF magic, starts the
stability launcher, then waits for temporary root, KernelSU late-load, and
`uid=0(root)`. The asset bundle is copied even when root is already active;
payload attempts are limited to one per launcher invocation.

`SLIDE_P0_OFFSET` may be set only to a known offset for the current boot. The
runner also accepts integer overrides for `FUTEX_WAIT_SEC`,
`KSNITCH_REPEAT`, `KSNITCH_APPENDED`, `PIPE_DETERMINISTIC`, and `RMG_TRACE_FILE`.

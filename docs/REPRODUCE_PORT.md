# Reproduce The Port

The easiest way to reproduce the port is to run:

```sh
./tools/port-sm-s918b-afzf5.sh
```

For the current laboratory firmware `S918BXXSAFZH3`, use the dedicated
two-stage port/tuning script:

```sh
./tools/port-sm-s918b-afzh3.sh --no-adb-check
```

The older AFZG1 profile remains bundled and is not rewritten by the AFZH3
script.

That command validates the connected device, patches the base payload, prepares
local app assets, and prints the manual ADB commands.

## Prerequisites

On the host:

```sh
adb version
python3 --version
java -version
```

You also need:

- Android SDK and Gradle working for the app build.
- `adb` available in `PATH`.
- USB debugging enabled and authorized on the device.
- A local upstream `rmg-f731u` checkout.
- The local base artifacts used by the script:
  `cve-2026-43499-app.so`, `ksud-f731u-kdp`, and `libcve43499root.so`.

By default, the script expects the upstream checkout at:

```text
~/Projects/rmg-f731u
```

Override it with:

```sh
UPSTREAM_REPO=/path/to/rmg-f731u ./tools/port-sm-s918b-afzf5.sh
```

## What The Script Does

### AFZH3 tuned payload

```text
1. Verify the F731U base payload.
2. Apply the AFZH3 symbol/offset port specification.
3. Verify the intermediate AFZH3 payload (md5 948c555b6ecbee22c035690955e8faf8).
4. Apply the tuned runtime specification to the validated AFZH3 payload.
5. Verify the new payload (md5 9115ebee17da0d070265e428c0424719).
6. Copy the new payload to the Android and desktop asset trees.
```

The tuned profile preserves the boot allocator quiet window and the validated
20 ms P-select timing. It removes redundant retries by selecting one attempt,
uses 45 seconds for P0 preparation, 60 seconds for the attempt, and reduces
only the external supervisor polling interval to 10 ms. The quiet window is
intentional: skipping it was tested as a fail-fast diagnostic and did not
produce a reliable root on a freshly booted AFZH3 device.

### Legacy AFZF5 script

Default command:

```sh
./tools/port-sm-s918b-afzf5.sh
```

Steps:

```text
1. Validate the connected ADB device against the SM-S918B target profile.
2. Verify the MD5 of the F731U base payload.
3. Apply tools/f731u-to-dm3q-s918b-afzf5.spec.json.
4. Generate out/dm3q-S918BXXSAFZF5/cve-2026-43499-app.so.
5. Verify the generated payload MD5 and size.
6. Create app/src/main/assets/support/targets-v2.json.
7. Copy local runtime assets into app/src/main/assets/payloads/.
8. Copy the native helper into app/src/main/jniLibs/arm64-v8a/.
9. Print manual ADB test commands.
```

## Patch Inputs

Default input paths:

```text
tools/patch_payload.py
tools/f731u-to-dm3q-s918b-afzf5.spec.json
tools/f731u-to-dm3q-s918b-afzh3.spec.json
tools/f731u-to-dm3q-s918b-afzh3-tuned.spec.json
~/Projects/rmg-f731u/app-src/app/src/main/assets/cve-2026-43499-app.so
~/Projects/rmg-f731u/app-src/app/src/main/assets/ksud-f731u-kdp
~/Projects/rmg-f731u/app-src/app/src/main/jniLibs/arm64-v8a/libcve43499root.so
```

Useful overrides:

```sh
BASE_PAYLOAD=/path/to/cve-2026-43499-app.so ./tools/port-sm-s918b-afzf5.sh
KSUD_PATH=/path/to/ksud-f731u-kdp ./tools/port-sm-s918b-afzf5.sh
HELPER_PATH=/path/to/libcve43499root.so ./tools/port-sm-s918b-afzf5.sh
OUT_DIR=/path/to/out ./tools/port-sm-s918b-afzf5.sh
PATCHER=/path/to/patch_payload.py ./tools/port-sm-s918b-afzf5.sh
SPEC=/path/to/spec.json ./tools/port-sm-s918b-afzf5.sh
```

## Generate Without A Connected Device

Generate local assets without checking ADB:

```sh
./tools/port-sm-s918b-afzf5.sh --no-adb-check
```

Generate only the patched payload:

```sh
./tools/port-sm-s918b-afzf5.sh --no-adb-check --no-app-assets --no-print
```

## Expected Checks

```text
base F731U payload:
  md5 3c82d4f678bd58846facf3e4ad356a33

ported dm3q/S918BXXSAFZF5 payload:
  md5 f6298194afb543d618b6f7015d1d08eb
  size 131072

ported dm3q/S918BXXSAFZH3 payload:
  md5 948c555b6ecbee22c035690955e8faf8
  size 131072

tuned dm3q/S918BXXSAFZH3 payload:
  md5 9115ebee17da0d070265e428c0424719
  size 131072

ksud-f731u-kdp:
  md5 bd9080bc728f3b98f0239236cd2e22ec
  size 6756208

libcve43499root.so:
  md5 08fad03af01e7a411154180f4b22385a
  size 23640
```

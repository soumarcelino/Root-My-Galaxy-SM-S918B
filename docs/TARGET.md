# Target Profile

This port targets one exact Samsung Galaxy S23 Ultra build. The Android app and
the helper script both use these values to avoid accidentally running the port
on a different firmware.

## Device

```text
model: SM-S918B
device: dm3q
manufacturer: samsung
build display: BP4A.251205.006.S918BXXSAFZF5
fingerprint: samsung/dm3qxxx/dm3q:16/BP4A.251205.006/S918BXXSAFZF5:user/release-keys
kernel release: 5.15.189-android13-8-33413713-abS918BXXSAFZF5
kernel build: #1 SMP PREEMPT Tue Jun 9 09:47:44 UTC 2026
Android SDK: 36
ABI: arm64-v8a
page size: 4096
```

## App Profile

The bundled app profile is generated at:

```text
app/src/main/assets/targets-v3.json
```

The important fields are:

```text
profileId: dm3q-S918BXXSAFZF5
manufacturer: samsung
model: SM-S918B
device: dm3q
kernelRelease: 5.15.189-android13-8-33413713-abS918BXXSAFZF5
kernelBuildVersion: #1 SMP PREEMPT Tue Jun 9 09:47:44 UTC 2026
buildDisplay: BP4A.251205.006.S918BXXSAFZF5
buildFingerprint: samsung/dm3qxxx/dm3q:16/BP4A.251205.006/S918BXXSAFZF5:user/release-keys
sdk: 36
abi: arm64-v8a
pageSize: 4096
```

## Verify A Connected Device

Run:

```sh
adb devices
adb shell 'getprop ro.product.model; getprop ro.product.device; getprop ro.build.display.id; uname -r; uname -v'
```

Expected values:

```text
SM-S918B
dm3q
BP4A.251205.006.S918BXXSAFZF5
5.15.189-android13-8-33413713-abS918BXXSAFZF5
#1 SMP PREEMPT Tue Jun 9 09:47:44 UTC 2026
```

The main script performs this validation automatically unless
`--no-adb-check` is passed.

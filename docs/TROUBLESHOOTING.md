# Troubleshooting

## `unexpected model`, `unexpected build display`, Or `unexpected kernel release`

The connected device does not match the validated profile.

Check:

```sh
adb shell 'getprop ro.product.model; getprop ro.product.device; getprop ro.build.display.id; uname -r; uname -v'
```

Expected:

```text
SM-S918B
dm3q
BP4A.251205.006.S918BXXSAFZF5
5.15.189-android13-8-33413713-abS918BXXSAFZF5
#1 SMP PREEMPT Tue Jun 9 09:47:44 UTC 2026
```

If you only want to generate files without a connected device, use:

```sh
./tools/port-sm-s918b-afzf5.sh --no-adb-check
```

## `base payload md5 mismatch`

The F731U base payload is not the same one used for this port.

Check the default path:

```text
~/Projects/rmg-f731u/app-src/app/src/main/assets/cve-2026-43499-app.so
```

Or pass an explicit path:

```sh
BASE_PAYLOAD=/path/to/cve-2026-43499-app.so ./tools/port-sm-s918b-afzf5.sh
```

Expected MD5:

```text
3c82d4f678bd58846facf3e4ad356a33
```

## `KernelSU daemon md5 mismatch`

The local `ksud-f731u-kdp` does not match the expected artifact.

Pass an explicit path:

```sh
KSUD_PATH=/path/to/ksud-f731u-kdp ./tools/port-sm-s918b-afzf5.sh
```

Expected MD5:

```text
bd9080bc728f3b98f0239236cd2e22ec
```

## `root helper md5 mismatch`

The local `libcve43499root.so` does not match the expected helper.

Pass an explicit path:

```sh
HELPER_PATH=/path/to/libcve43499root.so ./tools/port-sm-s918b-afzf5.sh
```

Expected MD5:

```text
08fad03af01e7a411154180f4b22385a
```

## App Fails After `preparing kernel access`

On this target, the app path can fail because the Android `untrusted_app`
SELinux domain is more restricted than the ADB shell domain. The observed
failure point is usually around kernel information discovery, trace, or slab
access.

The ADB shell path has different permissions and was the practical validation
path for this port.

## `Payload execution failed: 255` after `kernel-location-ready`

Do not use the old AFZH3 fail-fast experiment with a 2-second payload/helper
deadline for normal rooting. On this firmware the P0 preparation and the
privileged transition can legitimately take longer than two seconds; killing
the child at that point produces exit `255` without indicating a bad offset.

Use the AFZH3 tuned profile instead:

```text
payload: cve-2026-43499-app-afzh3-tuned.so
attempts: 1
P0_ATTEMPT_TIMEOUT_SEC: 45
EXPLOIT_ATTEMPT_TIMEOUT_SEC: 60
PSELECT_DELAY_USEC: 20000
```

The tuned profile keeps the boot allocator quiet window. A freshly booted
device may therefore wait until its uptime reaches the validated window before
the actual exploit work starts. The payload is version-locked to
`S918BXXSAFZH3`; do not use it on another build.

If ADB disappears during a laboratory run, wait for the device to finish
booting and collect `adb shell getprop ro.boot.bootreason` plus
`/sys/fs/pstore/*` before trying another payload.

## APK Builds But The App Does Not Find A Compatible Profile

Make sure this file exists before building:

```text
app/src/main/assets/support/targets-v2.json
```

It should contain:

```text
profileId: dm3q-S918BXXSAFZF5
model: SM-S918B
device: dm3q
buildDisplay: BP4A.251205.006.S918BXXSAFZF5
```

Regenerate it with:

```sh
./tools/port-sm-s918b-afzf5.sh --no-adb-check
```

## Shell Command Shows `syntax error: unexpected '&&'`

This usually happens when a quoted ADB command was split across lines by the
terminal. Keep the command on one line, or use the exact command printed by the
script.

## Log File Is Empty Or Missing

Check whether the staged directory exists:

```sh
adb shell 'ls -la /data/local/tmp/rmg-dm3q'
```

Then rerun staging:

```sh
./tools/port-sm-s918b-afzf5.sh --stage-adb
```

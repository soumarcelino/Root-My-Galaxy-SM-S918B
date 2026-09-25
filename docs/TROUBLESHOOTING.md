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

## APK Builds But The App Does Not Find A Compatible Profile

Make sure this file exists before building:

```text
app/src/main/assets/targets-v3.json
```

It should contain:

```text
payloadId: dm3q-S918BXXSAFZF5
models: [SM-S918B]
buildDisplays: [BP4A.251205.006.S918BXXSAFZF5]
```

Check that its artifact names and sizes match the files in `app/src/main/assets/`.

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

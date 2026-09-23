# Stability Launcher

Native Android launcher that checks device conditions before loading a root
payload. The implementation is in `stability-launcher.c`.

The launcher measures system load and resource availability, probes pipe
capacity, and then loads the supplied shared library through `LD_PRELOAD`.
Root acquisition is performed by the payload and its helper.

## Execution flow

1. Reject a nonempty inherited `LD_PRELOAD` environment variable.
2. Parse arguments and, unless running with `--check-only`, check the first
   four ELF magic bytes of both payload and helper.
3. Collect device metrics every three seconds. Require consecutive samples
   that satisfy all thresholds in the selected profile.
4. Create 480 pipes, set each capacity to 8 KiB, then resize each to 128 KiB.
   Close every pipe after the probe, including on failure.
5. After a successful pipe probe, require another complete sequence of stable
   samples. This is the cooldown phase.
6. Configure the payload environment and replace the launcher process with
   `/system/bin/true`. Android's dynamic linker loads the payload from
   `LD_PRELOAD`; the payload must support execution through a constructor.

An unstable or unreadable sample resets the consecutive-sample counter.
A failed pipe probe requires a new stable sequence before another probe.
After a successful probe, cooldown failures reset the sample counter but do
not repeat the pipe probe.

## Profiles

The default profile is called `relaxado` in logs. `--conservative` selects
the `conservador` profile. Values below are source defaults.

| Condition | Default | Conservative |
| --- | ---: | ---: |
| Android `sys.boot_completed` | `1` | `1` |
| Minimum uptime | 60 s | 120 s |
| Minimum available memory | 1 GiB | 2 GiB |
| Maximum temperature | 48 °C | 42 °C |
| Maximum runnable tasks | 8 | 4 |
| Maximum CPU PSI `some avg10` | 25 | 12 |
| Maximum memory PSI `some avg10` | 3 | 1 |
| Maximum I/O PSI `some avg10` | 5 | 2 |
| Maximum active and total `mm_struct` objects, each | 2048 | 1024 |
| Maximum estimated `mm_struct` slabs | 48 | 32 |
| Consecutive samples per phase | 3 | 5 |

Both profiles use a three-second sampling interval and a 300-second gate
wait budget, shared by baseline, pipe probes, and cooldown. Blocking reads
or system calls are not separately timed out.

`REL_MAX_MM_SLABS` and `REL_GATE_NAME` can be overridden at compile time.

## Metric sources

- `/proc/meminfo`: `MemAvailable`.
- `/sys/class/thermal/thermal_zone*/temp`: maximum readable positive value
  below 200,000 millidegrees Celsius; at least one valid sensor is required.
- `/proc/loadavg`: runnable task count and one-minute load average. The load
  average is logged but is not an acceptance threshold.
- `/proc/pressure/{cpu,memory,io}`: `some avg10` values.
- `/proc/uptime`: time since boot.
- Android system property `sys.boot_completed`.
- `/proc/slabinfo`: active and total `mm_struct` object counts. Slab count is
  estimated as total objects divided by objects per slab, rounded up.

All metric readers must succeed for a sample to be accepted. The launching
user must therefore have permission to read these interfaces, including
`/proc/slabinfo`. Before probing pipes, the launcher attempts to raise its
soft file-descriptor limit to the existing hard limit.

## Usage

Run these commands **on the Android device**, with binaries already staged:

```sh
/data/local/tmp/stability-launcher \
  --payload /data/local/tmp/ksu-payload \
  --helper /data/local/tmp/ksu-helper
```

For stricter thresholds, add `--conservative`.

Check device conditions without loading a payload:

```sh
/data/local/tmp/stability-launcher --check-only
```

`--check-only` still performs the pipe allocation and resizing probe. Payload
and helper arguments are optional in this mode and are not validated.

| Argument | Meaning |
| --- | --- |
| `--payload PATH` | Shared-library payload to load; required for execution. |
| `--helper PATH` | Helper path passed to the payload; required for execution. |
| `--check-only` | Run the gates and return without loading the payload. |
| `--conservative` | Use the conservative profile. |

Unknown arguments print usage and return exit code 2. There is no dedicated
`--help` option.

## Payload environment

Immediately before `execve`, the launcher sets:

| Variable | Value | Existing value |
| --- | --- | --- |
| `CVE43499_ROOT_HELPER` | Supplied helper path | Overwritten |
| `EXPLOIT_ATTEMPTS` | `1` | Overwritten |
| `P0_ATTEMPT_TIMEOUT_SEC` | `45` | Preserved if already set |
| `EXPLOIT_ATTEMPT_TIMEOUT_SEC` | `180` | Preserved if already set |
| `BOOT_QUIET_SEC` | `0` | Preserved if already set |
| `FUTEX_WAIT_SEC` | `1` | Preserved if already set |
| `KSNITCH_REPEAT` | `64` | Preserved if already set |
| `LD_PRELOAD` | Supplied payload path | Overwritten after initial rejection check |

Timeout variables are interpreted by the payload; the launcher does not
supervise execution after `execve`.

## Logs and exit codes

Logs are written to standard error with a `[launcher]` prefix. Sample logs
include the phase (`baseline` or `cooldown`), accepted-sample count, and
measured values. Pipe probes emit `pipe-gate=pass` or `pipe-gate=fail`.

| Exit code | Meaning before payload execution |
| --- | --- |
| `0` | `--check-only` completed successfully. |
| `1` | Gate timeout, clock failure, environment setup failure, or failed `execve`. |
| `2` | Invalid arguments, missing/invalid ELF files, or inherited `LD_PRELOAD`. |
| `130` | SIGINT or SIGTERM cancellation observed during the gate phase. |

After successful `execve`, termination behavior belongs to the loaded
payload and `/system/bin/true`, not to the gate loop.

## Build

Requires the Android NDK. Example for Linux hosts, ARM64, Android API 35:

```sh
mkdir -p build
"$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android35-clang" \
  -O2 -Wall -Wextra -fPIE -pie \
  stability-launcher.c -o build/stability-launcher
```

Run from this directory with `ANDROID_NDK_HOME` pointing to the installed NDK.
The resulting binary runs on Android, not on the Linux host.

## Limitations

Passing the gates indicates that the measured conditions met the configured
thresholds. It does not prove payload correctness, successful root, KernelSU
availability, or freedom from kernel panics.

ELF validation checks only the magic bytes; it does not verify architecture,
hash, firmware compatibility, or executable permissions. Paths should be
absolute and compatible with `LD_PRELOAD` syntax.

`EXPLOIT_ATTEMPTS=1` limits one payload invocation. The launcher keeps no
persistent boot history and does not prevent a caller from launching it again
in the same boot. Firmware matching, cross-run retry policy, staging, and
post-execution root verification belong to the surrounding workflow.

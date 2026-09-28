# ZZHL root snapshot — boot 8ed17a84

Target: Samsung SM-S918B/dm3q, firmware `S918BXXUAZZHL`, kernel
`5.15.197-android13-8-34343818-abS918BXXUAZZHL`.

Full boot ID: `8ed17a84-b88f-4d2e-86b3-64a1eff7b380`.

Important files:

- `vmlinux.btf`: live kernel BTF.
- `kallsyms.txt`: unmasked runtime symbols; valid only for this boot's KASLR.
- `config.gz`: running kernel configuration.
- `slabinfo.txt`: privileged allocator snapshot.
- `dmesg.txt`, `trace.txt`: privileged runtime logs.
- `last_kmsg.txt`, `panic-excerpt.txt`: previous-boot failure evidence.
- `preflight.json`: device/build/boot identity.
- `manifest.json`, `root-assets.sha256`: provenance and hashes.

Root-assisted futex witness measured on this boot:

```text
task        ffffff886b628000
pi_blocked  ffffffc03adb3b38
lock        ffffff8931e59a90
```

These three addresses are dynamic and must never be reused on another boot or
attempt. Stable BTF offsets used to derive them: `task_struct.pi_blocked_on =
0x8b0`, `rt_mutex_waiter.lock = 0x38`, `rt_mutex_waiter.task = 0x30`, and
`task_struct.pid = 0x5d8`.

Canonical complete capture remains under
`evidence/forensics/20260922T232331Z-8ed17a84/`.

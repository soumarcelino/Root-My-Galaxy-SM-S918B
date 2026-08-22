# Real lab payload source

Authorized isolated-lab source bundle for CVE-2026-43499. This is the real
payload implementation used to produce
`../assets/cve-2026-43499-app-lab-afzg1.so`; it contains no simulated stages.

## Exact target

```text
model: SM-S918B
device: dm3q
build: BP4A.251205.006.S918BXXSAFZG1
kernel: 5.15.189-android13-8-33413713-abS918BXXSAFZG1
ABI: arm64-v8a
page size: 4096
```

The offsets and physical-page fingerprint are build-specific. Do not use this
payload on another kernel, firmware, ABI, or page size.

## Build

```sh
make -C payload-real-src
make -C payload-real-src check
```

Output:

```text
payload-real-src/build/cve-2026-43499-app-dm3q-S918BXXSAFZG1.so
```

`check` inspects the ELF without loading it. Loading the shared object runs its
constructor and starts the payload, so it must remain limited to the authorized
QEMU lab.

The previous FZF5 profile remains available with:

```sh
make -C payload-real-src TARGET=dm3q-S918BXXSAFZF5
```

## Provenance

The readable GhostLock source was vendored from
`soumarcelino/Root-My-Galaxy-SM-S918B`, commit
`b5056e6271bd370a24950012e794c35835752ddd`, under Apache-2.0. Only files needed
to build the exact AFZG1 and FZF5 lab profiles are included here.

The AFZG1 physical-page fingerprint was generated from the exact QEMU lab
`boot.img` (SHA-256
`02b63e17241c2d9fdd05d5a318c1ebabeb9bb7759e0952aac91be6402e44027f`), whose
raw ARM64 kernel Image has SHA-256
`44d4cb2bc258c121b40cd222c12e5cfde4ec5c690d7ab41536bd64e48c82fb0f`.

This implementation can exercise a kernel vulnerability and may reboot or
corrupt a mismatched guest. Use snapshots and a disposable guest. Patch the
guest kernel to remediate the vulnerable rtmutex/futex path before using it
outside vulnerability research.

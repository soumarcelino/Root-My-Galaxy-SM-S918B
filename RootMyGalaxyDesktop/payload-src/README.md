# Open-source reconstructed payload

This directory contains the reproducible source for the Desktop asset
`../assets/cve-2026-43499-app-open.so`.

It is a clean-room reconstruction based on the original AArch64 payload ELF
with SHA-256
`9837d252b7c8faa933d26b66602905492b7e6cdb317b3459efd2ff5dea5582e5`.
It is not recovered original source and it does not provide real root.

Implemented behavior:

- waits until `CLOCK_BOOTTIME` reaches 120 seconds;
- supervises each attempt with a timeout;
- models preparation and kernel-location discovery;
- models stages 08-11 with explicit phases and precondition checks;
- validates stage-to-stage state with userspace-only synthetic tokens;
- supports deterministic failure injection through
  `SIMULATED_FAIL_STAGE=8`, `9`, `10`, or `11`;
- does not invoke exploit primitives, access kernel memory, alter credentials,
  open a root socket, or start the helper in the simulated stages.

Build the Android AArch64 shared object:

```sh
make -C payload-src
```

Validate the generated ELF and run the host-side simulation tests without
loading the Android payload:

```sh
make -C payload-src check
```

The resulting file is
`payload-src/libcve43499root-reconstructed.so`. The Desktop application keeps
the distributable copy under `assets/cve-2026-43499-app-open.so`.

`test-loader.c` is provided for explicit loader testing and is not built by
default because loading the shared object runs its constructor.

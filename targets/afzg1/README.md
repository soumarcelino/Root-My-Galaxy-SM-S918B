# AFZG1 artifacts

- `payload/cve-2026-43499-app.so`: AFZG1 payload; SHA-256 `9837d252b7c8faa933d26b66602905492b7e6cdb317b3459efd2ff5dea5582e5`.
- `helper/libcve43499root.so`: AFZG1 root helper; SHA-256 `1f721d0a9a7ba80cebde5faab32fa56feee4ba12f1e282170b8bbc6af5663999`.

The Android app retains its own copies for packaging. The desktop runner no
longer packages AFZG1 and always delegates to the exact ZZI8 `simple-root`
target for the latest One UI 9 Beta 2 firmware. `kernelsu-next/` contains the
separate KernelSU Next helper source and build.

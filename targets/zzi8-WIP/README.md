# SM-S918B ZZI8 target

Target identity: `S918BXXUAZZI8`, Android 17 / One UI 9, kernel `5.15.197-android13-8-34343818-abS918BXXUAZZI8`.

The boot image was reconstructed byte-exactly from Samsung's ZZHL→ZZI8 incremental OTA. `brazilian-open-payload-engine/src/target.h` contains the firmware contract. Its verifier checks the symbol table and BTF before compilation.

KernelSU Next v3.4.0 uses the same KMI and BTF layouts as ZZHL; the packaged module has an exact ZZI8 vermagic. The Android profile requires the exact build display, fingerprint, kernel release, and kernel version string.

Building and installing the APK does not execute the payload.

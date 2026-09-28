# SM-S918B ZZI8 target · latest One UI 9 Beta 2 firmware

Target identity: `S918BXXUAZZI8`, the latest One UI 9 Beta 2 firmware on
Android 17, with kernel `5.15.197-android13-8-34343818-abS918BXXUAZZI8`.

The boot image was reconstructed byte-exactly from Samsung's ZZHL→ZZI8
incremental OTA for the latest One UI 9 Beta 2 firmware.
`brazilian-open-payload-engine/src/target.h` contains the firmware contract. Its
verifier checks the symbol table and BTF before compilation.

KernelSU Next v3.4.0 uses the same KMI and BTF layouts as ZZHL; the packaged
module has an exact ZZI8 vermagic for the latest One UI 9 Beta 2 firmware. The
Android profile requires the exact build display, fingerprint, kernel release,
and kernel version string.

Building and installing the APK does not execute the payload.

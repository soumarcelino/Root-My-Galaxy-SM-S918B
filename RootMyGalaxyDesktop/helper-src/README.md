# ZZI8 standalone helper

This directory builds the same readable helper used by the exact
`SM-S918B / S918BXXUAZZI8` target, the latest One UI 9 Beta 2 firmware.
The canonical source is
[`targets/zzi8-WIP/helper/su_daemon.c`](../../targets/zzi8-WIP/helper/su_daemon.c).

Build it from the repository root:

```sh
make -C RootMyGalaxyDesktop/helper-src clean all
```

Output: `RootMyGalaxyDesktop/helper-src/helper-readable`
(Android/AArch64, API 35).

The desktop application always delegates execution to
[`simple-root/simple-root.sh`](../../simple-root/simple-root.sh). Both layers
independently require the exact ZZI8 model, device, build display, kernel
release, and kernel version before the payload may run. Regional fingerprint
prefixes are collected for diagnostics but do not block execution.

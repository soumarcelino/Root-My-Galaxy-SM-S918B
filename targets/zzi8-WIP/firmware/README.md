# ZZI8 firmware evidence · latest One UI 9 Beta 2 firmware

`boot.img.p` is the Samsung ZZHL→ZZI8 OTA patch for the latest One UI 9 Beta 2
firmware. `apply-bsdiff.py` reconstructs the boot image from the tracked ZZHL
source boot image. The reconstructed target SHA-1 is
`323431390971835bbd3b6a1f91346852df1fc05a`.

`kallsyms_ZZI8.kallsyms` and `vmlinux_ZZI8.btf` were extracted from the latest
One UI 9 Beta 2 firmware's reconstructed ZZI8 kernel and are the inputs for
`brazilian-open-payload-engine/tools/verify-zzi8-target.py`.

# ZZI8 firmware evidence

`boot.img.p` is the Samsung ZZHL→ZZI8 OTA patch. `apply-bsdiff.py` reconstructs the boot image from the tracked ZZHL source boot image. The reconstructed target SHA-1 is `323431390971835bbd3b6a1f91346852df1fc05a`.

`kallsyms_ZZI8.kallsyms` and `vmlinux_ZZI8.btf` were extracted from the reconstructed ZZI8 kernel and are the inputs for `open-payload-engine/tools/verify-zzi8-target.py`.

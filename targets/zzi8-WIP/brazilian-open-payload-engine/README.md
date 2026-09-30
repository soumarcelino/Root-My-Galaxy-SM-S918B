# ZZI8 Open Payload Engine · latest One UI 9 Beta 2 firmware

Fresh target for `SM-S918B / S918BXXUAZZI8`, the latest One UI 9 Beta 2
firmware, derived from the generic ZZHL engine. All firmware-bound values live
in `src/target.h`.

Validation:

```sh
../../../tools/bope-verify-target --header src/target.h \
  --elf ../firmware/vmlinux_ZZI8.elf --btf ../firmware/vmlinux_ZZI8.btf
make test-aar-read-plan test-pipe-plan test-umh-binfmt-plan \
  test-fops-layout test-compact-log
make so
```

The verifier checks the symbol/BTF contract and decodes the ARM64 reference to
the empty static usermode-helper path against the reconstructed ZZI8 kernel.
Device execution is intentionally separate from build/install.

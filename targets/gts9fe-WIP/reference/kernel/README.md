# Reference kernel evidence

Derived from the `X518UVLSFEZG3` boot image. The firmware archive itself
(9.7 GB, `X518UVLSFEZG3.zip`) is not committed: the contract and the KernelSU
retarget only need the symbol table and BTF, both of which are reproducible
from `boot.img` with `vmlinux-to-elf` plus `llvm-nm` and `pahole`.

```text
kernel release:  5.15.189-android13-3-33478785
kernel version:  #1 SMP PREEMPT Sat Jul 18 02:16:22 KST 2026
VA bits:         39 (CONFIG_ARM64_VA_BITS_39=y)
page size:       4096
base:            0xffffffc008000000
```

## Files

```text
kallsyms_gts9fe.txt                  127,923 symbols recovered from the kernel ELF
btf/vmlinux-X518UVLSFEZG3-5.15.189.btf  embedded BTF used for struct layouts
```

## Derivation notes

- `RANDOMIZE_MODULE_REGION_FULL` is unset and `MODVERSIONS=y`;
  `MODULE_FORCE_LOAD` is unset and `TRIM_UNUSED_KSYMS=y`, so the module must be
  retargeted with an exact vermagic instead of relying on `--force`.
- `CONFIG_ARM64_VA_BITS_39=y` gives a 39-bit VA space; `ZONE_DMA` is unset.
- Samsung KDP/DEFEX are present (`kdp_assign_pgd`, `kdp_usecount_dec_and_test`,
  both resolved by name), which is why the KernelSU Next build enables the
  Samsung KDP/RKP/DEFEX options.
- `DEBUG_INFO_BTF_MODULES=y`, so the module carries its own BTF.

## Calibration measured on hardware

- `worker_thread+0xbc` is the `sched_blocked_reason` caller on this kernel
  (not the `+0x78` inherited from the Snapdragon targets).
- KASLR granularity is `0x8000`; slides of `0x108000` and `0x18000` were
  observed across two boots.
- Slab geometry read from `/proc/slabinfo`: `mm_struct` 1024 B with 32 objects
  per 8-page slab (order 3, `0x400` stride); `skbuff_head_cache` 256 B with 32
  objects per 2 pages.

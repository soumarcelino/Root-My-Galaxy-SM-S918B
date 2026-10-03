#!/usr/bin/env bash
# Build KernelSU Next v3.4.0 (Samsung KDP/RKP/DEFEX patch) for SM-X518U /
# X518UVLSFEZG3, then embed the .ko into ksud for the late-load path.
set -euo pipefail

target_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
repo_dir=$(cd -- "$target_dir/../../.." && pwd)
ksu_src=${KSU_NEXT_SRC:-$repo_dir/../ksu-next}
out_dir="$target_dir/out/kernelsu-next-gts9fe-v3.4.0"
ddk=${DDK_IMAGE:-ghcr.io/ylarod/ddk-min:android13-5.15-20260828}
target_release='5.15.189-android13-3-33478785'

[[ -d "$ksu_src/.git" ]] || {
  echo "KernelSU Next source not found at $ksu_src (set KSU_NEXT_SRC)" >&2
  exit 1
}
[[ "$(git -C "$ksu_src" describe --tags)" == "v3.4.0" ]] || {
  echo "KernelSU Next source must be the v3.4.0 tag" >&2
  exit 1
}

# The Samsung KDP/RKP/DEFEX port lives in the target patch, not upstream.
patch_file="$target_dir/patches/KernelSU-Next-v3.4.0-samsung-gts9fe-kdp-rkp-defex.patch"
if ! git -C "$ksu_src" apply --reverse --check "$patch_file" 2>/dev/null; then
  git -C "$ksu_src" apply "$patch_file"
fi

mkdir -p "$out_dir"

podman run --rm \
  -v "$ksu_src:/ksu:Z" \
  -v "$out_dir:/out:Z" \
  -w /ksu/kernel \
  "$ddk" bash -lc '
    set -euo pipefail
    REL="'"$target_release"'"
    printf "#define UTS_RELEASE \"%s\"\n" "$REL" > "$KDIR/include/generated/utsrelease.h"
    printf "%s\n" "$REL" > "$KDIR/include/config/kernel.release"
    echo "[*] release: $(cat $KDIR/include/config/kernel.release)"
    make -j"$(nproc)" \
      CONFIG_KSU=m \
      CONFIG_KSU_SAMSUNG_KDP=y \
      CONFIG_KSU_SAMSUNG_RKP=y \
      CONFIG_KSU_SAMSUNG_DEFEX=y \
      CONFIG_KSU_SAMSUNG_NO_PATCH_TEXT=y \
      KCFLAGS="-D__ANDROID_COMMON_KERNEL__ -DCONFIG_DEBUG_INFO_BTF_MODULES=1" \
      CC=clang KBUILD_MODPOST_WARN=1 2>&1 | tail -40
    ls -la ./kernelsu.ko
    modinfo ./kernelsu.ko | head -8
    cp ./kernelsu.ko /out/android13-5.15_kernelsu.ko
  '

echo "=== module ==="
ls -la "$out_dir"

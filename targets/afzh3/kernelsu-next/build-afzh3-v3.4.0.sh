#!/usr/bin/env bash
set -euo pipefail

repo_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../../.." && pwd)
ksu_src=${KSU_NEXT_SRC:-/home/matias/Projects/github/KernelSU-Next-v3.4.0}
kernel_src=${SAMSUNG_KERNEL_SRC:-/home/matias/Projects/SM-S918B_16_Opensource/kernel_platform/common}
kernel_out=${KERNEL_OUT:-$repo_dir/targets/afzh3/kernelsu-next/out/afzh3-kernel}
clang_root=${CLANG_ROOT:-/home/matias/Projects/github/android-clang-r450784e}
ndk_root=${ANDROID_NDK_HOME:-/home/matias/Android/Sdk/ndk/28.2.13676358}
linker="$ndk_root/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android26-clang"
out_dir="$repo_dir/targets/afzh3/kernelsu-next/out/kernelsu-next-afzh3-v3.4.0"
patch_file="$repo_dir/targets/afzh3/kernelsu-next/patches/KernelSU-Next-v3.4.0-samsung-afzh3-kdp-rkp-defex.patch"
expected_commit=1a879d6a866f80b1fa1c1009a2ffa747873cbb5e
expected_release=5.15.189-android13-8-33413713-abS918BXXSAFZH3

[[ -f "$kernel_src/Makefile" ]] || { echo "AFZH3 kernel source missing" >&2; exit 1; }
[[ -f "$kernel_out/.config" && -s "$kernel_out/Module.symvers" ]] || {
  echo "prepared AFZH3 .config and Module.symvers required" >&2; exit 1;
}
[[ "$(cat "$kernel_out/include/config/kernel.release")" == "$expected_release" ]] || {
  echo "kernel output release does not match AFZH3: $expected_release" >&2; exit 1;
}
[[ -x "$clang_root/bin/clang" ]] || { echo "Android clang missing" >&2; exit 1; }
"$clang_root/bin/clang" --version | grep -q '8508608, based on r450784e' || {
  echo "expected Android clang r450784e (build 8508608)" >&2; exit 1;
}
[[ -x "$ndk_root/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-strip" ]] || {
  echo "Android NDK missing" >&2; exit 1;
}
[[ -x "$linker" ]] || { echo "Android NDK linker missing" >&2; exit 1; }
source_head=$(git -C "$ksu_src" rev-parse HEAD)
source_parent=$(git -C "$ksu_src" rev-parse HEAD^ 2>/dev/null || true)
if [[ "$source_head" != "$expected_commit" && "$source_parent" != "$expected_commit" ]]; then
  echo "KernelSU Next must be v3.4.0 or its direct AFZH3 patch commit" >&2
  exit 1
fi

if ! git -C "$ksu_src" apply --check --reverse "$patch_file" 2>/dev/null; then
  if [[ "$source_head" != "$expected_commit" ]]; then
    echo "AFZH3 patch missing from KernelSU Next commit $source_head" >&2
    exit 1
  fi
  git -C "$ksu_src" apply --check "$patch_file"
  git -C "$ksu_src" apply "$patch_file"
fi

env PATH="$clang_root/bin:/usr/bin:/bin" \
  make -C "$kernel_src" O="$kernel_out" M="$ksu_src/kernel" src="$ksu_src/kernel" \
  ARCH=arm64 LLVM=1 LLVM_IAS=1 \
  "CC=$clang_root/bin/clang" "LD=$clang_root/bin/ld.lld" \
  "AR=$clang_root/bin/llvm-ar" "NM=$clang_root/bin/llvm-nm" \
  "OBJCOPY=$clang_root/bin/llvm-objcopy" \
  "OBJDUMP=$clang_root/bin/llvm-objdump" \
  "READELF=$clang_root/bin/llvm-readelf" \
  "STRIP=$clang_root/bin/llvm-strip" \
  GOOGLE_BRANCH=android13-5.15 KMI_GENERATION=8 \
  LOCALVERSION=-33413713 BUILD_NUMBER=S918BXXSAFZH3 \
  KCFLAGS=-D__ANDROID_COMMON_KERNEL__ CONFIG_KSU=m \
  KBUILD_MODPOST_WARN=1 \
  CONFIG_KSU_SAMSUNG_KDP=y CONFIG_KSU_SAMSUNG_RKP=y \
  CONFIG_KSU_SAMSUNG_DEFEX=y CONFIG_KSU_SAMSUNG_NO_PATCH_TEXT=y \
  modules -j"${JOBS:-8}"

mkdir -p -- "$out_dir"
"$clang_root/bin/llvm-strip" --strip-debug \
  -o "$out_dir/android13-5.15_kernelsu.ko" "$ksu_src/kernel/kernelsu.ko"
module_release=$(modinfo -F vermagic "$out_dir/android13-5.15_kernelsu.ko")
[[ "$module_release" == "$expected_release "* ]] || {
  echo "built module vermagic does not match AFZH3: $module_release" >&2; exit 1;
}
install -D -m 0644 "$out_dir/android13-5.15_kernelsu.ko" \
  "$ksu_src/userspace/ksud/bin/aarch64/android13-5.15_kernelsu.ko"

(
  cd -- "$ksu_src/userspace/ksud"
  env CARGO_TARGET_AARCH64_LINUX_ANDROID_LINKER="$linker" \
    LIBCLANG_PATH="$ndk_root/toolchains/llvm/prebuilt/linux-x86_64/lib" \
    cargo build --locked --release --target aarch64-linux-android
)

"$ndk_root/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-strip" --strip-all \
  -o "$out_dir/ksud-next-v3.4.0" \
  "$ksu_src/userspace/ksud/target/aarch64-linux-android/release/ksud"

modinfo "$out_dir/android13-5.15_kernelsu.ko" | grep '^vermagic:'
(
  cd -- "$out_dir"
  sha256sum android13-5.15_kernelsu.ko ksud-next-v3.4.0 > SHA256SUMS
  cat SHA256SUMS
)

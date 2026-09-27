#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_dir="$(cd -- "$script_dir/../../.." && pwd)"
ksu_src=${KSU_NEXT_SRC:-/home/matias/Projects/github/KernelSU-Next-v3.4.0-zzhl}
kernel_src=${SAMSUNG_KERNEL_SRC:-/home/matias/Projects/SM-S918B_16_Opensource/kernel_platform/common}
kernel_out=${KERNEL_OUT:-$repo_dir/targets/afzh3/kernelsu-next/out/afzh3-kernel}
clang_root=${CLANG_ROOT:-/home/matias/Projects/github/android-clang-r450784e}
ndk_root=${ANDROID_NDK_HOME:-/home/matias/Android/Sdk/ndk/28.2.13676358}
linker="$ndk_root/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android26-clang"
out_dir="$script_dir/out/kernelsu-next-zzhl-v3.4.0"

base_release=5.15.189-android13-8-33413713-abS918BXXSAFZH3
target_release=5.15.197-android13-8-34343818-abS918BXXUAZZHL
expected_source=c300cb9d712126e8ac2a678014a1f2e8adbb9362

[[ "$(git -C "$ksu_src" rev-parse HEAD)" == "$expected_source" ]] || {
  echo "KernelSU source must be the reviewed Samsung v3.4.0 commit" >&2
  exit 1
}
[[ -z "$(git -C "$ksu_src" status --porcelain --untracked-files=no)" ]] || {
  echo "KernelSU tracked source is dirty" >&2
  exit 1
}
[[ -s "$kernel_out/Module.symvers" ]] || {
  echo "prepared Samsung android13-5.15 Module.symvers is required" >&2
  exit 1
}
[[ -x "$clang_root/bin/clang" && -x "$linker" ]] || {
  echo "required Android toolchains are missing" >&2
  exit 1
}

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

mkdir -p -- "$out_dir" "$script_dir/work"
"$clang_root/bin/llvm-strip" --strip-debug \
  -o "$script_dir/work/android13-5.15_kernelsu-base.ko" \
  "$ksu_src/kernel/kernelsu.ko"
"$script_dir/retarget-vermagic.py" \
  "$script_dir/work/android13-5.15_kernelsu-base.ko" \
  "$out_dir/android13-5.15_kernelsu.ko" \
  --from-release "$base_release" --to-release "$target_release"

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

"$script_dir/verify-zzhl-compat.py" \
  --module "$out_dir/android13-5.15_kernelsu.ko" \
  > "$out_dir/compatibility.json"
(
  cd -- "$out_dir"
  sha256sum android13-5.15_kernelsu.ko ksud-next-v3.4.0 > SHA256SUMS
  cat SHA256SUMS
)

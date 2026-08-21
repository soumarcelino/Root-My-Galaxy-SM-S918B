#!/usr/bin/env bash
set -euo pipefail

repo_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
ksu_src=${KSU_NEXT_SRC:?set KSU_NEXT_SRC to a clean KernelSU Next v3.3.0 checkout}
kernel_src=${SAMSUNG_KERNEL_SRC:?set SAMSUNG_KERNEL_SRC}
kernel_out=${KERNEL_OUT:?set KERNEL_OUT to a prepared Samsung output}
clang_root=${CLANG_ROOT:?set CLANG_ROOT to clang-r450784e}
ndk_root=${ANDROID_NDK_HOME:?set ANDROID_NDK_HOME}
out_dir=${OUT_DIR:-"$repo_dir/out/kernelsu-next-afzg1"}
patch_file="$repo_dir/kernelsu-next/patches/KernelSU-Next-v3.3.0-samsung-afzg1-kdp-rkp-defex.patch"
expected_commit=3b18216f71df189ab3d1b1ce0bdb21be1268e771
expected_release=5.15.189-android13-8-33413713-abS918BXXSAFZG1

[[ -f "$kernel_src/Makefile" ]] || { echo "kernel source missing" >&2; exit 1; }
[[ -f "$kernel_out/.config" ]] || { echo "prepared kernel output missing" >&2; exit 1; }
[[ -x "$clang_root/bin/clang" ]] || { echo "clang r450784e missing" >&2; exit 1; }
[[ "$(git -C "$ksu_src" rev-parse HEAD)" == "$expected_commit" ]] || {
  echo "KernelSU Next must be commit $expected_commit" >&2
  exit 1
}

if ! git -C "$ksu_src" apply --check --reverse "$patch_file" 2>/dev/null; then
  git -C "$ksu_src" apply --check "$patch_file"
  git -C "$ksu_src" apply "$patch_file"
fi

printf '%s\n' "$expected_release" >"$kernel_out/include/config/kernel.release"
printf '#define UTS_RELEASE "%s"\n' "$expected_release" >"$kernel_out/include/generated/utsrelease.h"

env PATH="$clang_root/bin:/usr/bin:/bin" \
  make -C "$kernel_src" O="$kernel_out" M="$ksu_src/kernel" src="$ksu_src/kernel" \
  ARCH=arm64 LLVM=1 LLVM_IAS=1 CONFIG_KSU=m \
  CONFIG_KSU_SAMSUNG_KDP=y CONFIG_KSU_SAMSUNG_RKP=y \
  CONFIG_KSU_SAMSUNG_DEFEX=y CONFIG_KSU_SAMSUNG_NO_PATCH_TEXT=y \
  KBUILD_MODPOST_WARN=1 modules -j"${JOBS:-8}"

mkdir -p -- "$out_dir"
"$clang_root/bin/llvm-strip" --strip-debug \
  -o "$out_dir/android13-5.15_kernelsu.ko" "$ksu_src/kernel/kernelsu.ko"
install -D -m 0644 "$out_dir/android13-5.15_kernelsu.ko" \
  "$ksu_src/userspace/ksud/bin/aarch64/android13-5.15_kernelsu.ko"

(
  cd -- "$ksu_src/userspace/ksud"
  env LIBCLANG_PATH="$ndk_root/toolchains/llvm/prebuilt/linux-x86_64/lib" \
    cargo build --locked --release --target aarch64-linux-android
)

"$ndk_root/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-strip" --strip-all \
  -o "$out_dir/ksud-next" \
  "$ksu_src/userspace/ksud/target/aarch64-linux-android/release/ksud"

cc="$ndk_root/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android26-clang"
"$cc" -fPIE -pie -O2 -g0 -Wall -Wextra \
  -Wno-unused-parameter -Wno-sign-compare -I"$repo_dir" -I"$repo_dir/src" \
  -DTARGET_HEADER='"kernelsu-next/helper/target-afzg1.h"' \
  "$repo_dir/kernelsu-next/helper/su_daemon-next.c" \
  "$repo_dir/kernelsu-next/helper/target_guard.c" \
  -ldl -o "$out_dir/ksu-helper-next"
"$ndk_root/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-strip" --strip-all \
  "$out_dir/ksu-helper-next"

modinfo "$out_dir/android13-5.15_kernelsu.ko" | grep '^vermagic:'
sha256sum "$out_dir/android13-5.15_kernelsu.ko" \
  "$out_dir/ksud-next" "$out_dir/ksu-helper-next"

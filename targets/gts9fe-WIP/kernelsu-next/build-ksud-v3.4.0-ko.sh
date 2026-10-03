#!/usr/bin/env bash
# Build ksud for SM-X518U / X518UVLSFEZG3 with the retargeted module embedded,
# which is what the app's --late-load path consumes.
set -euo pipefail

target_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
repo_dir=$(cd -- "$target_dir/../../.." && pwd)
ksu_src=${KSU_NEXT_SRC:-$repo_dir/../ksu-next}
out_dir="$target_dir/out/kernelsu-next-gts9fe-v3.4.0"
module="$out_dir/android13-5.15_kernelsu.ko"
ndk_root=${ANDROID_NDK_HOME:-$HOME/Android/Sdk/ndk/28.2.13676358}
linker="$ndk_root/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android26-clang"

[[ -f "$module" ]] || {
  echo "missing $module; run build-gts9fe-v3.4.0.sh first" >&2
  exit 1
}
[[ -x "$linker" ]] || { echo "Android NDK linker missing at $linker" >&2; exit 1; }
rustup target list --installed | grep -qx aarch64-linux-android || {
  echo "install Rust target: rustup target add aarch64-linux-android" >&2; exit 1
}

# ksud embeds the module from this fixed path at compile time.
install -D -m 0644 "$module" \
  "$ksu_src/userspace/ksud/bin/aarch64/android13-5.15_kernelsu.ko"

(
  cd -- "$ksu_src/userspace/ksud"
  env CARGO_TARGET_AARCH64_LINUX_ANDROID_LINKER="$linker" \
    LIBCLANG_PATH="$ndk_root/toolchains/llvm/prebuilt/linux-x86_64/lib" \
    BINDGEN_EXTRA_CLANG_ARGS_aarch64_linux_android="--sysroot=$ndk_root/toolchains/llvm/prebuilt/linux-x86_64/sysroot -I$ndk_root/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/include/aarch64-linux-android" \
    cross build --target aarch64-linux-android --release 2>&1 | tail -20
)

install -D -m 0755 \
  "$ksu_src/userspace/ksud/target/aarch64-linux-android/release/ksud" \
  "$out_dir/ksud-next-v3.4.0"

(
  cd -- "$out_dir"
  sha256sum android13-5.15_kernelsu.ko ksud-next-v3.4.0 > SHA256SUMS
)
echo "=== artifacts ==="
ls -la "$out_dir"

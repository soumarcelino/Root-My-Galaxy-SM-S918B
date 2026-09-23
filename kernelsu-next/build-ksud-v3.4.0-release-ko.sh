#!/usr/bin/env bash
set -euo pipefail

repo_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
ksu_src=${KSU_NEXT_SRC:-/home/matias/Projects/github/KernelSU-Next-v3.4.0}
ndk_root=${ANDROID_NDK_HOME:-/home/matias/Android/Sdk/ndk/28.2.13676358}
out_dir="$repo_dir/kernelsu-next/out/kernelsu-next-v3.4.0-release-ko"
module="$repo_dir/kernelsu-next/aarch64-android13-5.15_kernelsu.ko"
expected_commit=1a879d6a866f80b1fa1c1009a2ffa747873cbb5e
expected_sha=c6814bb47ebb853f39aa77bd0579f86e9c2f4c180b1de9ab5dd01f1c7403b723
linker="$ndk_root/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android26-clang"

[[ "$(git -C "$ksu_src" rev-parse HEAD)" == "$expected_commit" ]] || {
  echo "KernelSU Next source must be the v3.4.0 tag" >&2; exit 1;
}
[[ "$(sha256sum "$module" | cut -d' ' -f1)" == "$expected_sha" ]] || {
  echo "module does not match the official v3.4.0 asset" >&2; exit 1;
}
[[ -x "$linker" ]] || { echo "Android NDK linker missing" >&2; exit 1; }
rustup target list --installed | grep -qx aarch64-linux-android || {
  echo "install Rust target: rustup target add aarch64-linux-android" >&2; exit 1;
}

install -D -m 0644 "$module" \
  "$ksu_src/userspace/ksud/bin/aarch64/android13-5.15_kernelsu.ko"

(
  cd -- "$ksu_src/userspace/ksud"
  env CARGO_TARGET_AARCH64_LINUX_ANDROID_LINKER="$linker" \
    LIBCLANG_PATH="$ndk_root/toolchains/llvm/prebuilt/linux-x86_64/lib" \
    cargo build --locked --release --target aarch64-linux-android
)

mkdir -p -- "$out_dir"
install -m 0755 \
  "$ksu_src/userspace/ksud/target/aarch64-linux-android/release/ksud" \
  "$out_dir/ksud-next-v3.4.0"
sha256sum "$module" "$out_dir/ksud-next-v3.4.0"

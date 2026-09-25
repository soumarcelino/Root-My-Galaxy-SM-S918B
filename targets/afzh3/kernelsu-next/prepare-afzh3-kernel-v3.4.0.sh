#!/usr/bin/env bash
set -euo pipefail

repo_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../../.." && pwd)
source_root=${SAMSUNG_SOURCE_ROOT:-/home/matias/Projects/SM-S918B_16_Opensource}
kernel_src="$source_root/kernel_platform/common"
boot_image="$source_root/device-afzh3/boot-unpacked/kernel"
clang_root=${CLANG_ROOT:-/home/matias/Projects/github/android-clang-r450784e}
kernel_out=${KERNEL_OUT:-$repo_dir/targets/afzh3/kernelsu-next/out/afzh3-kernel}
expected_release=5.15.189-android13-8-33413713-abS918BXXSAFZH3
expected_boot_sha256=1f20782f0c0c91e329958d4715364c8f1682a42dc79aa4dce42a80f4aa3ae1d7

[[ -f "$kernel_src/Makefile" && -x "$kernel_src/scripts/extract-ikconfig" ]] || {
  echo "Samsung AFZH3 common kernel source missing: $kernel_src" >&2; exit 1;
}
[[ -f "$boot_image" ]] || { echo "AFZH3 boot kernel missing: $boot_image" >&2; exit 1; }
echo "$expected_boot_sha256  $boot_image" | sha256sum --check --status || {
  echo "AFZH3 boot kernel SHA256 mismatch" >&2; exit 1;
}
[[ -x "$clang_root/bin/clang" ]] || { echo "Android clang r450784e missing" >&2; exit 1; }
"$clang_root/bin/clang" --version | grep -q '8508608, based on r450784e' || {
  echo "expected Android clang r450784e (build 8508608)" >&2; exit 1;
}

mkdir -p -- "$kernel_out"
"$kernel_src/scripts/extract-ikconfig" "$boot_image" > "$kernel_out/.config"
[[ -s "$kernel_out/.config" ]] || { echo "boot image has no kernel config" >&2; exit 1; }

mapfile -t abi_lists < <(rg -o 'android/abi_gki_aarch64[[:alnum:]_]*' \
  "$kernel_src/build.config.gki.aarch64" | sort -u)
"$source_root/kernel_platform/build/abi/process_symbols" \
  --out-dir="$kernel_out" --out-file=abi_symbollist \
  --report-file=abi_symbollist.report --in-dir="$kernel_src" "${abi_lists[@]}"
"$source_root/kernel_platform/build/abi/flatten_symbol_list" \
  < "$kernel_out/abi_symbollist" > "$kernel_out/abi_symbollist.raw"
"$kernel_src/scripts/config" --file "$kernel_out/.config" \
  --set-str UNUSED_KSYMS_WHITELIST "$kernel_out/abi_symbollist.raw"

make_args=(
  -C "$kernel_src" O="$kernel_out" ARCH=arm64 LLVM=1 LLVM_IAS=1
  "CC=$clang_root/bin/clang" "LD=$clang_root/bin/ld.lld"
  "AR=$clang_root/bin/llvm-ar" "NM=$clang_root/bin/llvm-nm"
  "OBJCOPY=$clang_root/bin/llvm-objcopy"
  "OBJDUMP=$clang_root/bin/llvm-objdump"
  "READELF=$clang_root/bin/llvm-readelf"
  "STRIP=$clang_root/bin/llvm-strip"
  GOOGLE_BRANCH=android13-5.15 KMI_GENERATION=8
  LOCALVERSION=-33413713 BUILD_NUMBER=S918BXXSAFZH3
  KCFLAGS=-D__ANDROID_COMMON_KERNEL__
)
export PATH="$clang_root/bin:$PATH"
make "${make_args[@]}" olddefconfig
actual_release=$(make -s "${make_args[@]}" kernelrelease)
[[ "$actual_release" == "$expected_release" ]] || {
  echo "kernel release mismatch: $actual_release" >&2; exit 1;
}
echo "Building $actual_release from $kernel_src"
make "${make_args[@]}" -j"${JOBS:-4}" vmlinux
make "${make_args[@]}" -j"${JOBS:-4}" modules
[[ -s "$kernel_out/Module.symvers" ]] || {
  echo "Module.symvers was not generated" >&2; exit 1;
}
[[ "$(cat "$kernel_out/include/config/kernel.release")" == "$expected_release" ]] || {
  echo "built kernel release mismatch" >&2; exit 1;
}
echo "Prepared AFZH3 kernel output: $kernel_out"

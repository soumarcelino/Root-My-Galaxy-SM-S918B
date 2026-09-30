#!/usr/bin/env bash
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
COUNT="${1:-30}"
SERIAL="${SERIAL:-}"
PACKAGE="io.github.rootmygalaxy.s23ultra"
ACTIVITY="$PACKAGE/dev.busung.s25uroot.MainActivity"
PROFILE="dm3q-S918BXXUAZZI8-ksunext"
EXPECTED_BUILD="CP2A.260605.016.S918BXXUAZZI8"
EXPECTED_KERNEL="5.15.197-android13-8-34343818-abS918BXXUAZZI8"
RUN_TIMEOUT_SEC="${RUN_TIMEOUT_SEC:-600}"
RUN_PREFLIGHT="${RUN_PREFLIGHT:-1}"
STAMP="$(date +%Y%m%d-%H%M%S)"
OUTPUT="${OUTPUT:-$REPO_ROOT/validation/zzi8-app-campaign-$STAMP}"
REMOTE_PAYLOAD=/data/local/tmp/ksu-payload
REMOTE_HELPER=/data/local/tmp/ksu-helper
REMOTE_LAUNCHER=/data/local/tmp/stability-launcher
REMOTE_FACTORY=/data/local/tmp/mm-exec-factory

if [[ -n "$SERIAL" ]]; then
  ADB=(adb -s "$SERIAL")
else
  ADB=(adb)
fi

mkdir -p "$OUTPUT"
SUMMARY="$OUTPUT/summary.tsv"
printf 'run\tboot_id\tresult\tduration_sec\troot_id\tsu_id\tselinux\thistory\n' >"$SUMMARY"

adb_shell() {
  "${ADB[@]}" shell "$@" | tr -d '\r'
}

wait_for_boot() {
  "${ADB[@]}" wait-for-device >/dev/null
  local deadline=$((SECONDS + 180))
  while (( SECONDS < deadline )); do
    if [[ "$(adb_shell getprop sys.boot_completed 2>/dev/null || true)" == "1" ]]; then
      return 0
    fi
    sleep 2
  done
  return 1
}

newest_history() {
  "${ADB[@]}" exec-out run-as "$PACKAGE" sh -c \
    'ls -t files/install-history/*.json 2>/dev/null | head -n 1' \
    2>/dev/null | tr -d '\r'
}

read_history() {
  local path="$1"
  "${ADB[@]}" exec-out run-as "$PACKAGE" cat "$path" 2>/dev/null
}

start_shizuku() {
  local apk directory starter
  apk="$(adb_shell pm path moe.shizuku.privileged.api | sed -n 's/^package://p' | head -n 1)"
  [[ -n "$apk" ]] || return 1
  directory="${apk%/base.apk}"
  starter="$directory/lib/arm64/libshizuku.so"
  adb_shell "$starter" >"$1" 2>&1
  for _ in $(seq 1 30); do
    adb_shell pidof shizuku_server >/dev/null 2>&1 && return 0
    sleep 1
  done
  return 1
}

run_preflight() {
  local before_boot after_boot
  printf '[campaign] preflight rebooting\n'
  "${ADB[@]}" reboot >/dev/null || true
  wait_for_boot
  before_boot="$(adb_shell cat /proc/sys/kernel/random/boot_id)"
  "${ADB[@]}" push "$REPO_ROOT/app/src/main/assets/cve-2026-43499-app-zzi8.so" "$REMOTE_PAYLOAD" >/dev/null
  "${ADB[@]}" push "$REPO_ROOT/app/src/main/assets/cve-2026-43499-root-zzi8" "$REMOTE_HELPER" >/dev/null
  "${ADB[@]}" push "$REPO_ROOT/app/src/main/assets/stability-launcher-zzi8" "$REMOTE_LAUNCHER" >/dev/null
  "${ADB[@]}" push "$REPO_ROOT/app/src/main/assets/mm-exec-factory-zzi8" "$REMOTE_FACTORY" >/dev/null
  adb_shell chmod 755 "$REMOTE_PAYLOAD" "$REMOTE_HELPER" "$REMOTE_LAUNCHER" "$REMOTE_FACTORY"
  if ! "${ADB[@]}" shell \
      "PIPE_PREFLIGHT_ONLY=1 exec $REMOTE_LAUNCHER --payload $REMOTE_PAYLOAD --helper $REMOTE_HELPER --mm-factory $REMOTE_FACTORY" \
      >"$OUTPUT/preflight.log" 2>&1; then
    printf '[campaign] FAIL preflight command\n' >&2
    return 1
  fi
  after_boot="$(adb_shell cat /proc/sys/kernel/random/boot_id)"
  [[ "$before_boot" == "$after_boot" ]] || return 1
  grep -q '\[root_umh\] preflight ready' "$OUTPUT/preflight.log"
  grep -q '\[pipe_rw\] pre-mutation plan ready' "$OUTPUT/preflight.log"
  grep -q 'BOPE :: Preflight success' "$OUTPUT/preflight.log"
  printf '[campaign] PASS preflight boot=%s mutation=skipped\n' "$before_boot"
}

if [[ "$RUN_PREFLIGHT" == 1 ]]; then
  run_preflight
fi

for run in $(seq 1 "$COUNT"); do
  run_dir="$OUTPUT/run-$(printf '%02d' "$run")"
  mkdir -p "$run_dir"
  printf '[campaign] run=%d/%d rebooting\n' "$run" "$COUNT"
  "${ADB[@]}" reboot >/dev/null || true
  wait_for_boot

  boot_id="$(adb_shell cat /proc/sys/kernel/random/boot_id)"
  build="$(adb_shell getprop ro.build.display.id)"
  kernel="$(adb_shell uname -r)"
  printf '%s\n%s\n%s\n' "$boot_id" "$build" "$kernel" >"$run_dir/identity.txt"
  [[ "$build" == "$EXPECTED_BUILD" && "$kernel" == "$EXPECTED_KERNEL" ]]

  start_shizuku "$run_dir/shizuku.log"
  before="$(newest_history || true)"
  adb_shell am force-stop "$PACKAGE" >/dev/null
  started=$SECONDS
  adb_shell am start -W -n "$ACTIVITY" \
    --ez debug_campaign_install true \
    --es debug_campaign_profile_id "$PROFILE" >"$run_dir/am-start.txt"

  history=""
  result="Running"
  while (( SECONDS - started < RUN_TIMEOUT_SEC )); do
    if ! "${ADB[@]}" get-state >/dev/null 2>&1; then
      result="DeviceDisconnected"
      break
    fi
    history="$(newest_history || true)"
    if [[ -n "$history" && "$history" != "$before" ]]; then
      read_history "$history" >"$run_dir/history.json" || true
      result="$(jq -r '.result // "Invalid"' "$run_dir/history.json" 2>/dev/null || echo Invalid)"
      [[ "$result" == "Running" ]] || break
    fi
    sleep 2
  done

  duration=$((SECONDS - started))
  root_id="$(adb_shell '/data/local/tmp/ksu-helper -c id' 2>/dev/null || true)"
  su_id="$(adb_shell 'su -c id' 2>/dev/null || true)"
  selinux="$(adb_shell getenforce 2>/dev/null || true)"
  printf '%s\n' "$root_id" >"$run_dir/root-id.txt"
  printf '%s\n' "$su_id" >"$run_dir/su-id.txt"
  printf '%s\n' "$selinux" >"$run_dir/selinux.txt"

  printf '%d\t%s\t%s\t%d\t%s\t%s\t%s\t%s\n' \
    "$run" "$boot_id" "$result" "$duration" "$root_id" "$su_id" \
    "$selinux" "$history" >>"$SUMMARY"

  if [[ "$result" != "Succeeded" ]] ||
     ! grep -q 'BOPE :: Success' "$run_dir/history.json" ||
     ! grep -Eq '\[root_umh\] binfmt result .*trigger=1 socket=1 restore=1' "$run_dir/history.json" ||
     ! grep -q '\[root_umh\] preflight ready' "$run_dir/history.json" ||
     ! grep -q '\[app\] bootstrap-root=ready' "$run_dir/history.json" ||
     ! grep -q 'KernelSU control verified' "$run_dir/history.json" ||
     [[ "$su_id" != uid=0* ]] ||
     [[ "$selinux" != "Enforcing" ]]; then
    printf '[campaign] FAIL run=%d boot=%s result=%s\n' \
      "$run" "$boot_id" "$result" >&2
    exit 1
  fi
  printf '[campaign] PASS run=%d boot=%s duration=%ds\n' \
    "$run" "$boot_id" "$duration"
done

printf '[campaign] PASS %s/%s output=%s\n' "$COUNT" "$COUNT" "$OUTPUT"

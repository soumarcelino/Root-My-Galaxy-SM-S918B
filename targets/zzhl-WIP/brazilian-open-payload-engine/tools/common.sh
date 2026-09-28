#!/usr/bin/env bash

set -o pipefail

tool_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_dir="$(cd -- "$tool_dir/.." && pwd)"

die() {
  printf 'erro: %s\n' "$*" >&2
  exit 1
}

note() {
  printf '[*] %s\n' "$*" >&2
}

ok() {
  printf '[+] %s\n' "$*" >&2
}

need_cmd() {
  command -v "$1" >/dev/null 2>&1 || die "comando ausente: $1"
}

timestamp_utc() {
  date -u +%Y%m%dT%H%M%SZ
}

resolve_serial() {
  local requested="${1:-}" adb_bin="${ADB:-adb}"
  if [[ -n "$requested" ]]; then
    printf '%s\n' "$requested"
    return 0
  fi

  local devices
  mapfile -t devices < <("$adb_bin" devices | awk 'NR > 1 && $2 == "device" {print $1}')
  [[ ${#devices[@]} -eq 1 ]] ||
    die "use --serial; dispositivos ADB ativos encontrados: ${#devices[@]}"
  printf '%s\n' "${devices[0]}"
}

require_device() {
  local serial="$1" adb_bin="${ADB:-adb}"
  "$adb_bin" -s "$serial" get-state 2>/dev/null | tr -d '\r' | grep -qx device ||
    die "device ADB indisponível ou não autorizado: $serial"
}

adb_shell() {
  local serial="$1"
  shift
  "${ADB:-adb}" -s "$serial" shell "$@"
}

strip_cr() {
  tr -d '\r'
}

safe_name() {
  printf '%s' "$1" | tr -cs 'A-Za-z0-9._-' '_'
}

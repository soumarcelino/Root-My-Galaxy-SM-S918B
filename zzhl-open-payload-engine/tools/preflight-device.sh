#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=common.sh
source "$script_dir/common.sh"

usage() {
  cat <<'EOF'
Uso: preflight-device.sh [opções]

Coleta identidade e estado do device sem modificar o kernel.

Opções:
  --serial SERIAL          Serial ADB; obrigatório se houver mais de um device.
  --expect-build BUILD     Falha se ro.build.version.incremental divergir.
  --require-clean          Exige boot concluído e /system/bin/su ausente.
  --output ARQUIVO         Salva JSON; sem esta opção imprime em stdout.
  -h, --help               Mostra esta ajuda.
EOF
}

serial=
expect_build=
require_clean=0
output=
while [[ $# -gt 0 ]]; do
  case "$1" in
    --serial) serial="${2:?serial ausente}"; shift 2 ;;
    --expect-build) expect_build="${2:?build ausente}"; shift 2 ;;
    --require-clean) require_clean=1; shift ;;
    --output) output="${2:?arquivo ausente}"; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    *) die "opção desconhecida: $1" ;;
  esac
done

need_cmd "${ADB:-adb}"
need_cmd python3
serial="$(resolve_serial "$serial")"
require_device "$serial"

model="$(adb_shell "$serial" getprop ro.product.model | strip_cr)"
device="$(adb_shell "$serial" getprop ro.product.device | strip_cr)"
build="$(adb_shell "$serial" getprop ro.build.version.incremental | strip_cr)"
completed="$(adb_shell "$serial" getprop sys.boot_completed | strip_cr)"
boot_id="$(adb_shell "$serial" cat /proc/sys/kernel/random/boot_id | strip_cr)"
uptime="$(adb_shell "$serial" "cut -d' ' -f1 /proc/uptime" | strip_cr)"
kernel="$(adb_shell "$serial" uname -a | strip_cr)"
bootreason="$(adb_shell "$serial" getprop ro.boot.bootreason | strip_cr)"
soft_pipe="$(adb_shell "$serial" cat /proc/sys/fs/pipe-user-pages-soft 2>/dev/null | strip_cr || true)"
hard_pipe="$(adb_shell "$serial" cat /proc/sys/fs/pipe-user-pages-hard 2>/dev/null | strip_cr || true)"
threads_max="$(adb_shell "$serial" cat /proc/sys/kernel/threads-max 2>/dev/null | strip_cr || true)"

su_present=false
root_works=false
if adb_shell "$serial" 'test -x /system/bin/su' >/dev/null 2>&1; then
  su_present=true
  if adb_shell "$serial" "/system/bin/su -c 'id'" 2>/dev/null | grep -q 'uid=0(root)'; then
    root_works=true
  fi
fi
if [[ "$root_works" == true ]]; then
  [[ -n "$soft_pipe" ]] || soft_pipe="$(adb_shell "$serial" "/system/bin/su -c 'cat /proc/sys/fs/pipe-user-pages-soft'" 2>/dev/null | strip_cr || true)"
  [[ -n "$hard_pipe" ]] || hard_pipe="$(adb_shell "$serial" "/system/bin/su -c 'cat /proc/sys/fs/pipe-user-pages-hard'" 2>/dev/null | strip_cr || true)"
  [[ -n "$threads_max" ]] || threads_max="$(adb_shell "$serial" "/system/bin/su -c 'cat /proc/sys/kernel/threads-max'" 2>/dev/null | strip_cr || true)"
fi

[[ -z "$expect_build" || "$build" == "$expect_build" ]] ||
  die "build divergente: obtido=$build esperado=$expect_build"
if (( require_clean )); then
  [[ "$completed" == 1 ]] || die "boot ainda não concluído"
  [[ "$su_present" == false ]] || die "boot não está limpo: /system/bin/su presente"
fi

emit_json() {
  python3 - "$serial" "$model" "$device" "$build" "$completed" "$boot_id" \
    "$uptime" "$kernel" "$bootreason" "$su_present" "$root_works" \
    "$soft_pipe" "$hard_pipe" "$threads_max" <<'PY'
import json, sys
keys = [
    "serial", "model", "device", "build", "boot_completed", "boot_id",
    "uptime_seconds", "kernel", "bootreason", "su_present", "root_works",
    "pipe_user_pages_soft", "pipe_user_pages_hard", "threads_max",
]
data = dict(zip(keys, sys.argv[1:]))
for key in ("su_present", "root_works"):
    data[key] = data[key].lower() == "true"
data["boot_completed"] = data["boot_completed"] == "1"
for key in ("pipe_user_pages_soft", "pipe_user_pages_hard", "threads_max"):
    if data[key].isdigit():
        data[key] = int(data[key])
print(json.dumps(data, indent=2, ensure_ascii=False, sort_keys=True))
PY
}

if [[ -n "$output" ]]; then
  mkdir -p "$(dirname -- "$output")"
  emit_json > "$output"
  ok "preflight salvo: $output"
else
  emit_json
fi

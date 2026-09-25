#!/usr/bin/env bash
# Arm or collect a root-only tracefs proof for the external P0 RB node.
set -Eeuo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=common.sh
source "$script_dir/common.sh"

usage() {
  cat <<'EOF'
Uso: capture-p0-rb-geometry.sh --serial SERIAL --arm|--collect|--clear [--output DIR]

--arm       Cria probes em rt_mutex_adjust_pi e rb_erase; não executa payload.
--collect   Salva trace e decodifica geometria. Mantém probes até --clear.
--clear     Desabilita e remove os probes.

Requer root já disponível. A captura deve ocorrer antes de liberar o trigger P0.
EOF
}

serial=
output=
action=
while [[ $# -gt 0 ]]; do
  case "$1" in
    --serial) serial="${2:?serial ausente}"; shift 2 ;;
    --output) output="${2:?diretório ausente}"; shift 2 ;;
    --arm|--collect|--clear) action="${1#--}"; shift ;;
    -h|--help) usage; exit 0 ;;
    *) die "opção desconhecida: $1" ;;
  esac
done

[[ -n "$serial" && -n "$action" ]] || die "use --serial e uma ação"
require_device "$serial"
adb_shell "$serial" "/system/bin/su -c 'id'" 2>/dev/null | grep -q 'uid=0(root)' ||
  die "root necessário para tracefs kprobe"

trace_root="$(adb_shell "$serial" "/system/bin/su -c 'if [ -d /sys/kernel/tracing ]; then echo /sys/kernel/tracing; else echo /sys/kernel/debug/tracing; fi'" | strip_cr)"
events="$trace_root/kprobe_events"
event_dir="$trace_root/events/kprobes"
remote() { adb_shell "$serial" "/system/bin/su -c '$1'"; }

clear_probes() {
  remote "echo 0 > '$event_dir/p0rb_adjust/enable' 2>/dev/null || true"
  remote "echo 0 > '$event_dir/p0rb_erase/enable' 2>/dev/null || true"
  remote "echo '-:p0rb_adjust' > '$events' 2>/dev/null || true"
  remote "echo '-:p0rb_erase' > '$events' 2>/dev/null || true"
}

case "$action" in
  arm)
    clear_probes
    printf '%s\n' 'p:p0rb_adjust rt_mutex_adjust_pi task=$arg1:x64 blocked=+2224($arg1):x64' |
      "${ADB:-adb}" -s "$serial" shell /system/bin/su -c "cat > '$events'"
    printf '%s\n' 'p:p0rb_erase rb_erase node=$arg1:x64 root=$arg2:x64 parent=+0($arg1):x64 right=+8($arg1):x64 left=+16($arg1):x64' |
      "${ADB:-adb}" -s "$serial" shell /system/bin/su -c "cat > '$events'"
    remote "echo 1 > '$event_dir/p0rb_adjust/enable'"
    remote "echo 1 > '$event_dir/p0rb_erase/enable'"
    remote "echo > '$trace_root/trace'"
    ok "probes P0 RB armados; execute somente diagnóstico aprovado e depois use --collect"
    ;;
  collect)
    boot_id="$(adb_shell "$serial" cat /proc/sys/kernel/random/boot_id | strip_cr)"
    output="${output:-$repo_dir/evidence/zzhl-validation/p0-rb-geometry-${boot_id%%-*}}"
    mkdir -p "$output"
    "${ADB:-adb}" -s "$serial" exec-out /system/bin/su -c "cat '$trace_root/trace'" \
      > "$output/trace.txt"
    python3 "$script_dir/analyze-p0-rb-trace.py" "$output/trace.txt" \
      --output "$output/geometry.json"
    ok "geometria salva em $output"
    ;;
  clear)
    clear_probes
    ok "probes P0 RB removidos"
    ;;
esac
